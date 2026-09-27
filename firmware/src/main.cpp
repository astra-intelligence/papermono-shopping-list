// PaperMono Shopping List - an e-paper shopping list that syncs with a small server on the home
// network. See docs/architecture.md for how the pieces fit together.
//
// Everything runs on the Arduino loop task. Each iteration: service the board (LED timeouts, power
// button, battery), route one touch event to whichever screen is in front, page on the side buttons,
// then run a sync if one is due. A sync blocks the loop while it runs.

#include <Arduino.h>
#include <M5Unified.h>

#include "config.h"
#include "hal/bsp.h"
#include "hal/epd.h"
#include "hal/touch.h"
#include "model.h"
#include "sync/local_store.h"
#include "sync/sync_client.h"
#include "ui/keyboard.h"
#include "ui/list_screen.h"
#include "ui/quantity_screen.h"
#include "ui/settings_screen.h"

using namespace ShoppingList;

namespace {

ShoppingData g_data;
ListScreen g_list;

uint32_t g_nextSyncMs = 0;          // periodic sync due time
bool g_syncRequested = false;       // a local edit (or SYNC NOW) wants a sync as soon as possible
uint32_t g_syncCooldownUntilMs = 0; // set after a failed sync
uint32_t g_lastBatteryCheckMs = 0;

constexpr uint32_t kBatteryCheckIntervalMs = 5000;
constexpr uint32_t kKeyDebounceMs = 40;

// Reports a physical button press once, after the reading has been stable for kKeyDebounceMs.
struct DebouncedKey {
    bool rawPrev = false;
    bool stable = false;
    uint32_t lastChangeMs = 0;

    bool pressed(bool raw, uint32_t now) {
        if (raw != rawPrev) {
            rawPrev = raw;
            lastChangeMs = now;
        }
        if (raw != stable && (now - lastChangeMs) >= kKeyDebounceMs) {
            stable = raw;
            return stable; // press edge only
        }
        return false;
    }
};
DebouncedKey g_key1;
DebouncedKey g_key2;

bool overlayOpen() {
    return KeyboardWidget::getInstance().isOpen() || SettingsScreen::getInstance().isOpen() ||
           QuantityScreen::getInstance().isOpen();
}

// Redraws the list with the high-quality waveform. Reserved for moments where content worth reading
// has changed (boot, a sync that changed something, closing an overlay); everything interactive
// uses partial updates instead.
void showListFull() {
    g_list.draw(M5.Display);
    Epd::getInstance().fullRefresh();
}

void runSync(uint32_t wifiTimeoutMs) {
    // Compare before and after so a periodic sync that changed nothing doesn't flash the panel.
    const std::vector<Category> previousCategories = g_data.categories;
    const std::vector<Item> previousItems = g_data.items;

    // Painted immediately, before the blocking call below - a tap-triggered sync (see
    // Config::kTapSyncStaleMs) can otherwise take several seconds of Wi-Fi connect + HTTP with no
    // feedback, which looks like the tap did nothing.
    g_list.setSyncing(true);
    const bool ok = SyncClient::getInstance().sync(g_data, wifiTimeoutMs);
    g_list.setSyncing(false);
    if (ok) {
        BSP::getInstance().setLedSyncOk();
        g_syncCooldownUntilMs = 0;
        g_list.onDataChanged();
        if (g_data.categories != previousCategories || g_data.items != previousItems) showListFull();
    } else {
        BSP::getInstance().setLedSyncFailed();
        g_syncCooldownUntilMs = millis() + Config::kFailedSyncCooldownMs;
    }
    g_nextSyncMs = millis() + Config::kSyncIntervalMs;
    g_syncRequested = false;
}

void maybeSync() {
    // Never while an overlay is open: the keyboard holds pointers into g_data.catalog, which a sync
    // replaces (see keyboard.h), and yanking Settings away mid-read would be jarring anyway.
    if (overlayOpen()) return;

    const uint32_t now = millis();
    const bool periodicDue = (int32_t)(now - g_nextSyncMs) >= 0;
    const bool cooledDown = (int32_t)(now - g_syncCooldownUntilMs) >= 0;
    if ((g_syncRequested || periodicDue) && cooledDown) {
        // Someone just tapped something: fail fast if we're out of range.
        runSync(g_syncRequested ? Config::kInteractiveWifiTimeoutMs : Config::kWifiConnectTimeoutMs);
    }
}

void openSettings() {
    const SyncStatus& sync = SyncClient::getInstance().status();
    SettingsSnapshot snapshot;
    snapshot.wifiSsid = Config::kWifiSsid;
    snapshot.everSynced = sync.everAttempted;
    snapshot.lastSyncOk = sync.lastOk;
    snapshot.lastSyncAgoSec = sync.everAttempted ? (millis() - sync.lastAttemptMs) / 1000 : 0;
    snapshot.batteryPercent = BSP::getInstance().getBatteryState().percentage;
    snapshot.frontlightPercent = BSP::getInstance().getFrontlight();

    SettingsScreen::getInstance().open(
        snapshot, [](uint8_t percent) { BSP::getInstance().setFrontlight(percent); },
        []() {
            g_syncRequested = true;
            g_syncCooldownUntilMs = 0;
        });
}

void handleTouch() {
    TouchManager& touch = TouchManager::getInstance();
    if (!touch.hasEvent()) return;
    const TouchEvent ev = touch.popEvent();

    // Any tap is a sign someone's actively using the device - opportunistically sync if the last one
    // is getting stale, rather than leaving the list stale for the rest of what's now a full hour
    // between periodic syncs. Safe to request unconditionally even while an overlay is open:
    // maybeSync() only actually runs a sync once overlayOpen() is false.
    if (ev.type == TouchEventType::Click) {
        const SyncStatus& status = SyncClient::getInstance().status();
        if (status.lastSuccessMs == 0 ||
            (int32_t)(millis() - status.lastSuccessMs) >= (int32_t)Config::kTapSyncStaleMs) {
            g_syncRequested = true;
        }
    }

    const bool wasOverlay = overlayOpen();
    if (KeyboardWidget::getInstance().isOpen()) KeyboardWidget::getInstance().handleTouch(ev);
    else if (SettingsScreen::getInstance().isOpen()) SettingsScreen::getInstance().handleTouch(ev);
    else if (QuantityScreen::getInstance().isOpen()) QuantityScreen::getInstance().handleTouch(ev);
    else g_list.handleTouch(ev);

    // An overlay stays painted on the panel until something repaints over it. Every way of closing
    // one (SEND, tap outside, swipe, CLOSE, SYNC NOW) ends up here, so repaint once, here.
    if (wasOverlay && !overlayOpen()) showListFull();
}

void handleKeys() {
    if (overlayOpen()) return;
    const uint32_t now = millis();
    if (g_key1.pressed(digitalRead(Pins::KEY1) == LOW, now)) g_list.page(-1);
    if (g_key2.pressed(digitalRead(Pins::KEY2) == LOW, now)) g_list.page(1);
}

void checkBattery() {
    const uint32_t now = millis();
    if (now - g_lastBatteryCheckMs < kBatteryCheckIntervalMs) return;
    g_lastBatteryCheckMs = now;

    const BatteryState state = BSP::getInstance().getBatteryState();
    if (!state.isCharging && state.voltageMv <= BSP::LOW_BATTERY_CUTOFF_MV) {
        BSP::getInstance().lowBatteryShutdown(); // does not return
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("PaperMono Shopping List booting...");

    BSP::getInstance().init();
    Epd::getInstance().init();
    TouchManager::getInstance().init();
    LocalStore::getInstance().init();

    // Show whatever was cached last time straight away; the first sync runs on the first loop.
    LocalStore::getInstance().loadCache(g_data);
    g_list.bind(&g_data);
    g_list.setSyncRequestCallback([]() { g_syncRequested = true; });
    g_list.setOpenSettingsCallback(openSettings);
    showListFull();

    g_nextSyncMs = millis();
}

void loop() {
    BSP::getInstance().update();
    TouchManager::getInstance().update();
    checkBattery();

    if (BSP::getInstance().checkPowerButton()) BSP::getInstance().powerOff();

    handleTouch();
    handleKeys();
    maybeSync();

    delay(20);
}
