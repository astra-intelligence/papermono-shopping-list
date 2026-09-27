#include "sync/local_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_log.h>

#include "config.h"
#include "sync/model_json.h"

static constexpr const char* TAG = "Store";

static constexpr const char* kCategoriesPath = "/cats.json";
static constexpr const char* kItemsPath = "/items.json";
static constexpr const char* kCatalogPath = "/catalog.json";
static constexpr const char* kPendingPath = "/pending.json";

namespace ShoppingList {
namespace {

// A missing, empty or corrupt file reads as an empty array.
JsonDocument readJsonArray(const char* path) {
    JsonDocument doc;
    File f = LittleFS.open(path, "r");
    if (!f || deserializeJson(doc, f) != DeserializationError::Ok || !doc.is<JsonArray>()) {
        doc.to<JsonArray>();
    }
    return doc;
}

void writeJson(const char* path, const JsonDocument& doc) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return;
    }
    serializeJson(doc, f);
}

template <typename T>
void writeArrayFile(const char* path, const std::vector<T>& values) {
    JsonDocument doc;
    ModelJson::writeArray(values, doc.to<JsonArray>());
    writeJson(path, doc);
}

template <typename T>
std::vector<T> readArrayFile(const char* path, T (*from)(JsonObjectConst)) {
    const JsonDocument doc = readJsonArray(path);
    return ModelJson::readArray(doc.as<JsonArrayConst>(), from);
}

} // namespace

bool LocalStore::init() {
    // formatOnFail: the partition is blank after a fresh flash.
    if (!LittleFS.begin(true)) {
        ESP_LOGE(TAG, "Failed to mount LittleFS");
        return false;
    }
    return true;
}

void LocalStore::saveCache(const ShoppingData& data) {
    writeArrayFile(kCategoriesPath, data.categories);
    writeArrayFile(kItemsPath, data.items);
    writeArrayFile(kCatalogPath, data.catalog);
    ESP_LOGI(TAG, "Cached %u categories, %u items, %u catalog entries", (unsigned)data.categories.size(),
             (unsigned)data.items.size(), (unsigned)data.catalog.size());
}

bool LocalStore::loadCache(ShoppingData& data) {
    data.categories = readArrayFile(kCategoriesPath, ModelJson::categoryFrom);
    data.items = readArrayFile(kItemsPath, ModelJson::itemFrom);
    data.catalog = readArrayFile(kCatalogPath, ModelJson::catalogEntryFrom);
    return !data.categories.empty() || !data.items.empty();
}

std::vector<PendingAction> LocalStore::loadPending() {
    return readArrayFile(kPendingPath, ModelJson::pendingActionFrom);
}

void LocalStore::replacePending(const std::vector<PendingAction>& pending) {
    writeArrayFile(kPendingPath, pending);
}

void LocalStore::appendPending(const PendingAction& action) {
    std::vector<PendingAction> pending = loadPending();
    if (pending.size() >= Config::kMaxPendingActions) {
        ESP_LOGW(TAG, "Pending queue full (%u); dropping oldest", (unsigned)Config::kMaxPendingActions);
        pending.erase(pending.begin());
    }
    pending.push_back(action);
    replacePending(pending);
}

void LocalStore::queueAdd(const String& name, int categoryId, bool hasQuantity, float quantity,
                          const String& unit) {
    PendingAction a;
    a.type = PendingActionType::AddItem;
    a.name = name;
    a.categoryId = categoryId;
    a.hasQuantity = hasQuantity;
    a.quantity = quantity;
    a.unit = unit;
    appendPending(a);
}

void LocalStore::queueToggle(int itemId, bool purchased) {
    PendingAction a;
    a.type = PendingActionType::TogglePurchased;
    a.itemId = itemId;
    a.purchased = purchased;
    appendPending(a);
}

void LocalStore::queueSetQuantity(int itemId, bool hasQuantity, float quantity, const String& unit) {
    PendingAction a;
    a.type = PendingActionType::SetQuantity;
    a.itemId = itemId;
    a.hasQuantity = hasQuantity;
    a.quantity = quantity;
    a.unit = unit;
    appendPending(a);
}

} // namespace ShoppingList
