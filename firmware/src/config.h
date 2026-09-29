#pragma once

// Build-time configuration. Per-install values (Wi-Fi, server address) live in secrets.h, which is
// git-ignored: copy secrets.example.h to secrets.h and fill it in before building.

#include <cstdint>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing firmware/src/secrets.h - copy secrets.example.h to secrets.h and fill in your Wi-Fi and server details."
#endif

namespace ShoppingList {
namespace Config {

constexpr const char* kWifiSsid = SHOPPING_LIST_WIFI_SSID;
constexpr const char* kWifiPassword = SHOPPING_LIST_WIFI_PASSWORD;
constexpr const char* kServerBaseUrl = SHOPPING_LIST_SERVER_URL;

#ifndef FW_VERSION
#error "FW_VERSION isn't defined - it's set in platformio.ini's build_flags."
#endif
constexpr const char* kFirmwareVersion = FW_VERSION;

// Wi-Fi is only switched on for the length of one sync. A periodic sync can afford to wait; one
// triggered by a tap uses the shorter timeout, because the main loop (touch included) blocks for the
// whole connect attempt and the common real-world failure is being out of range in the shop.
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kInteractiveWifiTimeoutMs = 5000;
constexpr uint32_t kHttpTimeoutMs = 8000;

constexpr uint32_t kSyncIntervalMs = 60UL * 60UL * 1000UL; // periodic wake; on-demand sync also runs
// After a failed sync, don't retry for this long, so repeated taps don't each pay the full timeout.
constexpr uint32_t kFailedSyncCooldownMs = 30UL * 1000UL;
// A tap anywhere on the shopping list opportunistically syncs if the last one is older than this -
// otherwise, with the periodic wake now a full hour, a stale list would just sit there for the rest
// of that hour while someone's actively using the device.
constexpr uint32_t kTapSyncStaleMs = 5UL * 60UL * 1000UL;

// Force a full-panel refresh after this many fast partial updates. Unbounded partial refreshes
// build up ghosting and DC imbalance on the SSD1677 panel; ~10 is the usual guidance for this
// hardware.
constexpr uint16_t kMaxPartialRefreshes = 10;

// OTA updates (see sync/ota.h). Only started with this much battery left, or on charge, so a flash
// can't run out of power halfway.
constexpr int kOtaMinBatteryPercent = 50;
// After a failed attempt, don't try again for this long, so a bad link can't turn every sync into a
// download-and-fail loop.
constexpr uint32_t kOtaRetryAfterFailureMs = 24UL * 60UL * 60UL * 1000UL;
// Give up on a download that hasn't delivered a byte for this long.
constexpr uint32_t kOtaStallTimeoutMs = 15000;

// Upper bound on queued offline edits; normal use keeps 0-3.
constexpr size_t kMaxPendingActions = 100;

} // namespace Config
} // namespace ShoppingList
