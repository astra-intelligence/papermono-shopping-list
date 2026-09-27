#pragma once

// JSON <-> model mapping, shared by the sync client (server responses) and the local store (the
// on-flash cache), so the field names are written down exactly once. They must match the server's
// models.py.

#include <ArduinoJson.h>
#include <vector>

#include "model.h"

namespace ShoppingList {
namespace ModelJson {

Category categoryFrom(JsonObjectConst o);
Item itemFrom(JsonObjectConst o);
CatalogEntry catalogEntryFrom(JsonObjectConst o);
PendingAction pendingActionFrom(JsonObjectConst o);

void write(const Category& c, JsonObject o);
void write(const Item& it, JsonObject o);
void write(const CatalogEntry& ce, JsonObject o);
void write(const PendingAction& a, JsonObject o);

template <typename T>
std::vector<T> readArray(JsonArrayConst arr, T (*from)(JsonObjectConst)) {
    std::vector<T> out;
    out.reserve(arr.size());
    for (JsonObjectConst o : arr) out.push_back(from(o));
    return out;
}

template <typename T>
void writeArray(const std::vector<T>& values, JsonArray arr) {
    for (const auto& v : values) write(v, arr.add<JsonObject>());
}

} // namespace ModelJson
} // namespace ShoppingList
