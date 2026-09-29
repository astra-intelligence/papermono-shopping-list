#pragma once

// Syncs with the server over the home network. Wi-Fi is off the rest of the time and is brought up
// only for the length of one sync() call.
//
// A sync is: connect -> replay queued offline edits -> fetch a full /api/sync snapshot -> disconnect.
// It blocks the caller (and therefore touch handling) for its whole duration.

#include <cstdint>

#include "model.h"
#include "sync/ota.h"

namespace ShoppingList {

struct SyncStatus {
    bool everAttempted = false;
    bool lastOk = false;
    uint32_t lastAttemptMs = 0;
    uint32_t lastSuccessMs = 0; // 0 = never succeeded since boot
    bool lastReachedWifi = false; // Wi-Fi connected on the last attempt, so a failure was the server's
};

class SyncClient {
public:
    static SyncClient& getInstance() {
        static SyncClient instance;
        return instance;
    }

    // On success, replaces `data` with the server's state (plus any adds still waiting in the
    // queue) and caches it to flash. On failure `data` is left untouched and queued edits are kept.
    //
    // Replacing `data` reallocates its vectors: nothing may hold pointers into them across this call
    // (see KeyboardWidget, which does while it's open).
    bool sync(ShoppingData& data, uint32_t wifiTimeoutMs);

    const SyncStatus& status() const { return _status; }

    // The firmware update the last successful sync offered, if any; consumed by the call. The sync
    // only records the offer - acting on it is main.cpp's decision, after the list is safely saved.
    FirmwareOffer takeFirmwareOffer() {
        FirmwareOffer offer = _offer;
        _offer = FirmwareOffer();
        return offer;
    }

    // Wi-Fi up/down, shared with Ota, which needs the network outside of a sync.
    bool connectWifi(uint32_t timeoutMs);
    void disconnectWifi();

private:
    SyncClient() = default;

    // What to do with a queued edit after trying to send it.
    enum class PushResult {
        Done,      // 2xx
        Retry,     // transport error or 5xx: keep it queued
        Drop,      // 4xx: the server will never accept it (e.g. the item was deleted), so retrying
                   // would block everything queued behind it forever
    };

    void replayPending();
    PushResult push(const PendingAction& action);
    bool fetch(ShoppingData& data);

    SyncStatus _status;
    FirmwareOffer _offer;
};

} // namespace ShoppingList
