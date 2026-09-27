#include "sync/model_json.h"

namespace ShoppingList {
namespace ModelJson {

Category categoryFrom(JsonObjectConst o) {
    Category c;
    c.id = o["id"] | 0;
    c.name = o["name"].as<String>();
    c.sortOrder = o["sort_order"] | 0;
    return c;
}

Item itemFrom(JsonObjectConst o) {
    Item it;
    it.id = o["id"] | kPendingId;
    it.name = o["name"].as<String>();
    it.categoryId = o["category_id"] | kNoCategory;  // null (orphaned aisle) -> kNoCategory
    it.categoryName = o["category_name"].as<String>();
    it.purchased = o["purchased"] | false;
    it.hasQuantity = !o["quantity"].isNull();
    it.quantity = o["quantity"] | 0.0f;
    it.unit = o["unit"].isNull() ? String() : o["unit"].as<String>();
    return it;
}

CatalogEntry catalogEntryFrom(JsonObjectConst o) {
    CatalogEntry ce;
    ce.name = o["name"].as<String>();
    ce.categoryId = o["category_id"] | kNoCategory;
    ce.categoryName = o["category_name"].as<String>();
    return ce;
}

PendingAction pendingActionFrom(JsonObjectConst o) {
    PendingAction a;
    a.type = static_cast<PendingActionType>(o["type"] | 0);
    a.itemId = o["item_id"] | kPendingId;
    a.purchased = o["purchased"] | false;
    a.name = o["name"].as<String>();
    a.categoryId = o["category_id"] | kNoCategory;
    a.hasQuantity = o["has_quantity"] | false;
    a.quantity = o["quantity"] | 0.0f;
    a.unit = o["unit"].as<String>();
    return a;
}

void write(const Category& c, JsonObject o) {
    o["id"] = c.id;
    o["name"] = c.name;
    o["sort_order"] = c.sortOrder;
}

void write(const Item& it, JsonObject o) {
    o["id"] = it.id;
    o["name"] = it.name;
    o["category_id"] = it.categoryId;
    o["category_name"] = it.categoryName;
    o["purchased"] = it.purchased;
    if (it.hasQuantity) {
        o["quantity"] = it.quantity;
        o["unit"] = it.unit;
    } else {
        o["quantity"] = (const char*)nullptr;
        o["unit"] = (const char*)nullptr;
    }
}

void write(const CatalogEntry& ce, JsonObject o) {
    o["name"] = ce.name;
    o["category_id"] = ce.categoryId;
    o["category_name"] = ce.categoryName;
}

void write(const PendingAction& a, JsonObject o) {
    o["type"] = static_cast<int>(a.type);
    o["item_id"] = a.itemId;
    o["purchased"] = a.purchased;
    o["name"] = a.name;
    o["category_id"] = a.categoryId;
    o["has_quantity"] = a.hasQuantity;
    o["quantity"] = a.quantity;
    o["unit"] = a.unit;
}

} // namespace ModelJson
} // namespace ShoppingList
