#pragma once

// In-memory data model. Mirrors the server's /api/sync payload (server/src/shopping_list/models.py);
// the JSON mapping for both the network and the on-flash cache is in sync/model_json.h.

#include <Arduino.h>
#include <vector>

namespace ShoppingList {

struct Category {
    int id = 0;
    String name;
    int sortOrder = 0;

    bool operator==(const Category& other) const {
        return id == other.id && name == other.name && sortOrder == other.sortOrder;
    }
};

// Items added on the device get kPendingId until a sync gives them a real server id.
constexpr int kPendingId = -1;
constexpr int kNoCategory = -1;

struct Item {
    int id = kPendingId;
    String name;
    int categoryId = kNoCategory;
    String categoryName;
    bool purchased = false;
    bool hasQuantity = false; // false = no quantity tracked for this item
    float quantity = 0.0f;    // meaningful only if hasQuantity
    String unit;              // empty = plain count (e.g. "x3"); meaningful only if hasQuantity

    bool isPending() const { return id == kPendingId; }

    bool operator==(const Item& other) const {
        return id == other.id && name == other.name && categoryId == other.categoryId &&
               purchased == other.purchased && hasQuantity == other.hasQuantity &&
               quantity == other.quantity && unit == other.unit;
    }
};

// One previously-added item name and the aisle it was filed under. Drives autosuggest.
struct CatalogEntry {
    String name;
    int categoryId = kNoCategory;
    String categoryName;
};

// Everything the device knows about the list: the last successful sync plus any local additions.
struct ShoppingData {
    std::vector<Category> categories;
    std::vector<Item> items;
    std::vector<CatalogEntry> catalog;
};

enum class PendingActionType : uint8_t {
    AddItem = 0,
    TogglePurchased = 1,
    SetQuantity = 2,
};

// An edit made while offline, queued on flash and replayed at the start of the next sync.
struct PendingAction {
    PendingActionType type = PendingActionType::AddItem;
    int itemId = kPendingId;       // TogglePurchased/SetQuantity only
    bool purchased = false;        // TogglePurchased only
    String name;                   // AddItem only
    int categoryId = kNoCategory;  // AddItem only; kNoCategory lets the server classify it
    bool hasQuantity = false;      // AddItem + SetQuantity
    float quantity = 0.0f;         // AddItem + SetQuantity; meaningful only if hasQuantity
    String unit;                   // AddItem + SetQuantity; meaningful only if hasQuantity
};

} // namespace ShoppingList
