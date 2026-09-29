#pragma once

// Over-the-air firmware updates. The server offers an image in its /api/sync response (see
// docs/architecture.md#firmware-updates-ota); SyncClient parses the offer and main.cpp decides when to
// act on it, after the sync has saved and shown the list.
//
// Rollback is the bootloader's: main.cpp keeps a freshly installed image "pending verify" until the
// first successful sync calls confirmRunning(). If the device resets before that, the bootloader
// reverts to the previous image. On top of that, a version that got rolled back is remembered and
// never offered again, or the server would just re-offer it on the next sync.

#include <Arduino.h>

#include <cstdint>

namespace ShoppingList {

// Mirrors the "firmware" object in the /api/sync response.
struct FirmwareOffer {
    String version;
    String url; // relative to Config::kServerBaseUrl
    String sha256; // lowercase hex
    uint32_t size = 0;

    bool valid() const { return version.length() > 0 && url.length() > 0 && sha256.length() == 64 && size > 0; }
};

class Ota {
public:
    static Ota& getInstance() {
        static Ota instance;
        return instance;
    }

    // Call once from setup(). Works out whether the previous boot's update was rolled back.
    void begin();

    // Call after a successful sync: the running image can reach the server, so it's good. Cancels the
    // bootloader's pending rollback if there is one; does nothing otherwise.
    void confirmRunning();

    // Whether to act on `offer` now: not one we've already rolled back, not soon after a failed
    // attempt, and enough battery (or on charge).
    bool shouldInstall(const FirmwareOffer& offer, int batteryPercent, bool charging) const;

    // Downloads the image, checks its size and sha256, switches the boot slot and reboots. Brings up
    // Wi-Fi itself. Returns only on failure, having left the running firmware alone. Blocks for the
    // whole download.
    bool install(const FirmwareOffer& offer);

private:
    Ota() = default;

    bool download(const FirmwareOffer& offer);

    String _badVersion; // last version the bootloader rolled back; "" if none
    bool _backoff = false;
    uint32_t _retryAfterMs = 0;
};

} // namespace ShoppingList
