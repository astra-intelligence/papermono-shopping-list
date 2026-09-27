#pragma once

// Full-screen settings page, opened by tapping the list's header: frontlight level, a manual
// "sync now", and enough status (Wi-Fi network, last sync result, battery) to diagnose a device
// that isn't syncing without plugging it in.

#include <functional>
#include <string>

#include <M5Unified.h>

#include "hal/touch.h"

namespace ShoppingList {

struct SettingsSnapshot {
    std::string wifiSsid;
    bool everSynced = false;
    bool lastSyncOk = false;
    uint32_t lastSyncAgoSec = 0; // meaningful only if everSynced
    int batteryPercent = -1;     // -1 = unknown
    uint8_t frontlightPercent = 0;
};

class SettingsScreen {
public:
    static SettingsScreen& getInstance() {
        static SettingsScreen instance;
        return instance;
    }

    // `onFrontlightChange` fires on every step, so the light changes live. `onSyncNow` fires when
    // SYNC NOW is tapped; the screen closes and main.cpp runs the sync on its next loop.
    void open(const SettingsSnapshot& snapshot, std::function<void(uint8_t)> onFrontlightChange,
              std::function<void()> onSyncNow);
    void close() { _open = false; }
    bool isOpen() const { return _open; }

    void handleTouch(const TouchEvent& ev);

private:
    SettingsScreen() = default;

    void draw(M5GFX& gfx);

    bool _open = false;
    SettingsSnapshot _snapshot;
    std::function<void(uint8_t)> _onFrontlightChange;
    std::function<void()> _onSyncNow;
    int _frontlightStep = 0;
};

} // namespace ShoppingList
