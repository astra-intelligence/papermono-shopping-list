#pragma once

// On-flash persistence: the last synced list (so the device can render and autosuggest with Wi-Fi
// off between syncs, and after a reboot) plus the queue of edits made since.
//
// Stored as JSON files on LittleFS rather than in NVS/Preferences, because NVS caps a single value
// at ~4000 bytes - a catalog of only ~60 grocery names would already exceed that.

#include <vector>

#include "model.h"

namespace ShoppingList {

class LocalStore {
public:
    static LocalStore& getInstance() {
        static LocalStore instance;
        return instance;
    }

    bool init();

    void saveCache(const ShoppingData& data);
    // Returns false (with `data` emptied) if there's nothing cached yet.
    bool loadCache(ShoppingData& data);

    // Offline edits, replayed in order by SyncClient.
    void queueAdd(const String& name, int categoryId, bool hasQuantity, float quantity,
                  const String& unit);
    void queueToggle(int itemId, bool purchased);
    void queueSetQuantity(int itemId, bool hasQuantity, float quantity, const String& unit);
    std::vector<PendingAction> loadPending();
    void replacePending(const std::vector<PendingAction>& pending);

private:
    LocalStore() = default;

    void appendPending(const PendingAction& action);
};

} // namespace ShoppingList
