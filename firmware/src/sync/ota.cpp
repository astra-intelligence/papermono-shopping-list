#include "sync/ota.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "sync/sync_client.h"

static constexpr const char* TAG = "OTA";

// NVS namespace and keys. "attempt" is set for the length of an update: from just before the boot slot
// is switched until the new image confirms itself. "bad" outlives it if the image never did.
static constexpr const char* kNvsNamespace = "ota";
static constexpr const char* kKeyAttempt = "attempt";
static constexpr const char* kKeyBad = "bad";

namespace ShoppingList {

void Ota::begin() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        ESP_LOGW(TAG, "couldn't open NVS; rollback bookkeeping is off");
        return;
    }
    const String attempt = prefs.getString(kKeyAttempt, "");
    // An update was started but we're not running it: the bootloader reverted it (it reset before
    // confirming), or the slot switch never happened. Either way, don't take it again.
    if (attempt.length() > 0 && attempt != Config::kFirmwareVersion) {
        ESP_LOGW(TAG, "update to %s didn't stick, running %s; won't offer it again", attempt.c_str(),
                 Config::kFirmwareVersion);
        prefs.putString(kKeyBad, attempt);
        prefs.remove(kKeyAttempt);
    }
    _badVersion = prefs.getString(kKeyBad, "");
    prefs.end();
}

void Ota::confirmRunning() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "firmware %s confirmed", Config::kFirmwareVersion);
    }
    // Only touch NVS when there's something to clear: this runs after every sync, and flash wears.
    Preferences prefs;
    if (prefs.begin(kNvsNamespace, false)) {
        if (prefs.isKey(kKeyAttempt)) prefs.remove(kKeyAttempt);
        prefs.end();
    }
}

bool Ota::shouldInstall(const FirmwareOffer& offer, int batteryPercent, bool charging) const {
    if (!offer.valid()) return false;
    if (offer.version == Config::kFirmwareVersion) return false;
    if (offer.version == _badVersion) {
        ESP_LOGW(TAG, "ignoring offer of %s: it was rolled back before", offer.version.c_str());
        return false;
    }
    if (_backoff && (int32_t)(millis() - _retryAfterMs) < 0) return false;
    if (!charging && batteryPercent < Config::kOtaMinBatteryPercent) {
        ESP_LOGI(TAG, "not updating to %s: battery at %d%%", offer.version.c_str(), batteryPercent);
        return false;
    }
    return true;
}

bool Ota::install(const FirmwareOffer& offer) {
    ESP_LOGI(TAG, "updating %s -> %s (%u bytes)", Config::kFirmwareVersion, offer.version.c_str(),
             (unsigned)offer.size);
    bool ok = false;
    if (SyncClient::getInstance().connectWifi(Config::kWifiConnectTimeoutMs)) {
        ok = download(offer);
        SyncClient::getInstance().disconnectWifi();
    }
    if (!ok) {
        _backoff = true;
        _retryAfterMs = millis() + Config::kOtaRetryAfterFailureMs;
        return false;
    }
    ESP_LOGI(TAG, "update installed, rebooting into %s", offer.version.c_str());
    delay(200); // let the log line out
    ESP.restart();
    return true; // not reached
}

static String toHex(const uint8_t* bytes, size_t len) {
    static const char digits[] = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 0x0f];
    }
    return out;
}

bool Ota::download(const FirmwareOffer& offer) {
    // The URL is relative to the server we already sync with. Refuse anything else, so a response can't
    // point the device at another host (or, with "//", a protocol-relative one).
    if (!offer.url.startsWith("/") || offer.url.startsWith("//")) {
        ESP_LOGE(TAG, "refusing non-relative firmware URL '%s'", offer.url.c_str());
        return false;
    }

    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    if (slot == nullptr) {
        ESP_LOGE(TAG, "no OTA slot - is this the OTA partition table?");
        return false;
    }
    if (offer.size > slot->size) {
        ESP_LOGE(TAG, "image is %u bytes, slot holds %u", (unsigned)offer.size, (unsigned)slot->size);
        return false;
    }

    HTTPClient http;
    http.setTimeout(Config::kHttpTimeoutMs);
    if (!http.begin(String(Config::kServerBaseUrl) + offer.url)) return false;
    const int code = http.GET();
    if (code != 200) {
        ESP_LOGE(TAG, "GET %s failed (%d)", offer.url.c_str(), code);
        http.end();
        return false;
    }
    if (http.getSize() != (int)offer.size) {
        ESP_LOGE(TAG, "Content-Length %d doesn't match the offered size %u", http.getSize(),
                 (unsigned)offer.size);
        http.end();
        return false;
    }
    if (!Update.begin(offer.size, U_FLASH)) {
        ESP_LOGE(TAG, "Update.begin failed: %s", Update.errorString());
        http.end();
        return false;
    }

    // Hash while streaming into the slot, so there's no second pass over 1+ MB of flash.
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t remaining = offer.size;
    uint32_t lastDataMs = millis();
    bool ok = true;
    while (remaining > 0) {
        const size_t available = stream->available();
        if (available == 0) {
            if (!http.connected() || millis() - lastDataMs > Config::kOtaStallTimeoutMs) {
                ESP_LOGE(TAG, "download stalled with %u bytes to go", (unsigned)remaining);
                ok = false;
                break;
            }
            delay(1);
            continue;
        }
        const size_t want = min(min(available, sizeof(buf)), remaining);
        const size_t got = stream->readBytes(buf, want);
        mbedtls_sha256_update_ret(&sha, buf, got);
        if (Update.write(buf, got) != got) {
            ESP_LOGE(TAG, "flash write failed: %s", Update.errorString());
            ok = false;
            break;
        }
        remaining -= got;
        lastDataMs = millis();
    }
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (ok && !toHex(digest, sizeof(digest)).equalsIgnoreCase(offer.sha256)) {
        ESP_LOGE(TAG, "sha256 mismatch: got %s", toHex(digest, sizeof(digest)).c_str());
        ok = false;
    }
    if (!ok) {
        Update.abort();
        return false;
    }

    // Record the attempt before the slot switch, not after: if power dies between the two the device
    // still reboots into the new image, and needs to know what it is if that turns out to be bad.
    Preferences prefs;
    if (prefs.begin(kNvsNamespace, false)) {
        prefs.putString(kKeyAttempt, offer.version);
        prefs.end();
    }
    if (!Update.end(false)) { // validates the image, then makes the slot the boot slot
        ESP_LOGE(TAG, "Update.end failed: %s", Update.errorString());
        if (prefs.begin(kNvsNamespace, false)) {
            prefs.remove(kKeyAttempt);
            prefs.end();
        }
        return false;
    }
    return true;
}

} // namespace ShoppingList
