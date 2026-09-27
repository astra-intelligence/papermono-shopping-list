#include "ui/list_screen.h"

#include <algorithm>

#include <esp_log.h>

#include "hal/epd.h"
#include "sync/local_store.h"
#include "sync/sync_client.h"
#include "ui/keyboard.h"
#include "ui/quantity_screen.h"
#include "ui/widgets.h"

static constexpr const char* TAG = "List";

namespace ShoppingList {
namespace {

constexpr int kAisleLineH = 36;
constexpr int kItemLineH = UI::kRowH + UI::kGap;
constexpr int kContentBottom = 729;
constexpr int kAddBarY = 733;
constexpr int kAddBarH = 62;
// Right-edge zone of each item card reserved for the quantity display/tap target - tapping here
// opens the quantity screen instead of toggling purchased (see handleTouch()).
constexpr int kQtyZoneW = 110;
// The add bar splits into a wide ADD ITEM button and a narrower SETTINGS button, rather than relying
// only on the header-tap shortcut - a labeled button beats an entry point nobody notices.
constexpr int kAddBarGap = 12;
constexpr int kSettingsBtnW = 120;

std::string formatQuantity(const Item& item) {
    if (!item.hasQuantity) return "";
    // The device only ever writes whole-number quantities (see QuantityScreen), but a fractional
    // value set from the web app should still display accurately rather than being silently rounded.
    char numBuf[16];
    if (item.quantity == (float)(int)item.quantity) {
        snprintf(numBuf, sizeof(numBuf), "%d", (int)item.quantity);
    } else {
        snprintf(numBuf, sizeof(numBuf), "%.1f", item.quantity);
    }
    char buf[24];
    if (item.unit.length() > 0) snprintf(buf, sizeof(buf), "%s %s", numBuf, item.unit.c_str());
    else snprintf(buf, sizeof(buf), "x%s", numBuf);
    return buf;
}

// The area redrawn when the list changes or scrolls (a little above kContentTop to catch the rule).
constexpr int kContentAreaY = UI::kContentTop - 4;
constexpr int kContentAreaH = kContentBottom - kContentAreaY;

int lineHeight(bool isHeader) { return isHeader ? kAisleLineH : kItemLineH; }

void formatAge(char* out, size_t len, const char* prefix, uint32_t sinceMs) {
    const uint32_t sec = (millis() - sinceMs) / 1000;
    if (sec < 60) snprintf(out, len, "%s %us ago", prefix, (unsigned)sec);
    else snprintf(out, len, "%s %um ago", prefix, (unsigned)(sec / 60));
}

} // namespace

void ListScreen::bind(ShoppingData* data) {
    _data = data;
    _scrollOffset = 0;
    rebuildLines();
}

void ListScreen::onDataChanged() {
    rebuildLines();
    // Clamp rather than reset: a background sync shouldn't throw you back to page one.
    if (_lines.empty()) _scrollOffset = 0;
    else if (_scrollOffset >= (int)_lines.size()) _scrollOffset = (int)_lines.size() - 1;
}

void ListScreen::rebuildLines() {
    _lines.clear();
    if (_data == nullptr) return;

    // Items arrive grouped by aisle; start a new header whenever the aisle changes. Items added on
    // the device that the server hasn't confirmed yet go in their own group at the end.
    const String* currentAisle = nullptr;
    std::vector<int> pending;
    for (int i = 0; i < (int)_data->items.size(); ++i) {
        const Item& item = _data->items[i];
        if (item.isPending()) {
            pending.push_back(i);
            continue;
        }
        if (currentAisle == nullptr || item.categoryName != *currentAisle) {
            _lines.push_back({true, item.categoryName, -1});
            currentAisle = &item.categoryName;
        }
        _lines.push_back({false, item.name, i});
    }
    if (!pending.empty()) {
        _lines.push_back({true, "PENDING SYNC", -1});
        for (int i : pending) _lines.push_back({false, _data->items[i].name, i});
    }
}

void ListScreen::draw(M5GFX& gfx) {
    gfx.fillScreen(TFT_WHITE);
    drawHeader(gfx);
    drawContent(gfx);
    drawAddBar(gfx);
}

void ListScreen::drawHeader(M5GFX& gfx) {
    UI::header(gfx, "SHOPPING LIST", 3);

    char syncText[24];
    if (_syncing) {
        snprintf(syncText, sizeof(syncText), "syncing...");
    } else {
        const SyncStatus& status = SyncClient::getInstance().status();
        if (status.lastSuccessMs == 0) snprintf(syncText, sizeof(syncText), "not synced yet");
        else formatAge(syncText, sizeof(syncText), "synced", status.lastSuccessMs);
    }
    gfx.setTextColor(TFT_BLACK);
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextSize(1);
    gfx.drawString(syncText, UI::kContentX + UI::kContentW, UI::kHeaderY + UI::kHeaderH / 2);
}

void ListScreen::setSyncing(bool syncing) {
    _syncing = syncing;
    drawHeader(M5.Display);
    Epd::getInstance().partialUpdate(0, UI::kHeaderY, UI::kScreenW, UI::kHeaderH);
}

void ListScreen::drawContent(M5GFX& gfx) {
    gfx.fillRect(0, kContentAreaY, UI::kScreenW, kContentAreaH, TFT_WHITE);

    if (_lines.empty()) {
        gfx.setTextColor(TFT_BLACK);
        gfx.setTextDatum(textdatum_t::middle_center);
        gfx.setTextSize(3);
        gfx.drawString("Your list is empty", UI::kScreenW / 2, (UI::kContentTop + kContentBottom) / 2);
        return;
    }

    int y = UI::kContentTop;
    for (size_t i = _scrollOffset; i < _lines.size(); ++i) {
        const Line& line = _lines[i];
        if (y + lineHeight(line.isHeader) > kContentBottom) break;

        if (line.isHeader) {
            gfx.setTextColor(TFT_BLACK);
            gfx.setTextDatum(textdatum_t::bottom_left);
            gfx.setTextSize(2);
            gfx.drawString(line.text.c_str(), UI::kContentX, y + kAisleLineH - 8);
        } else {
            const Item& item = _data->items[line.itemIndex];
            const uint16_t bg = item.isPending() ? TFT_LIGHTGRAY : TFT_WHITE;
            UI::card(gfx, UI::kContentX, y, UI::kContentW, UI::kRowH, bg);

            constexpr int kBox = 28;
            const int boxX = UI::kContentX + 14;
            const int boxY = y + (UI::kRowH - kBox) / 2;
            gfx.drawRoundRect(boxX, boxY, kBox, kBox, 5, TFT_BLACK);
            if (item.purchased) gfx.fillRoundRect(boxX + 2, boxY + 2, kBox - 4, kBox - 4, 3, TFT_BLACK);

            const int textX = boxX + kBox + 14;
            const int textY = y + UI::kRowH / 2;
            gfx.setTextColor(TFT_BLACK, bg);
            gfx.setTextDatum(textdatum_t::middle_left);
            gfx.setTextSize(3);
            gfx.drawString(item.name.c_str(), textX, textY);
            if (item.purchased) gfx.drawFastHLine(textX, textY, gfx.textWidth(item.name.c_str()), TFT_BLACK);

            // Quantity zone: the tappable right edge of the card (see kQtyZoneW / handleTouch).
            // Shows the formatted value in black when set, or a muted "qty" placeholder when not -
            // both are equally tappable, so the placeholder is what makes "you can set a quantity
            // here" discoverable at all.
            const std::string qtyText = formatQuantity(item);
            gfx.setTextDatum(textdatum_t::middle_right);
            if (item.hasQuantity) {
                gfx.setTextColor(TFT_BLACK, bg);
                gfx.setTextSize(3);
                gfx.drawString(qtyText.c_str(), UI::kContentX + UI::kContentW - 14, textY);
            } else {
                gfx.setTextColor(TFT_LIGHTGRAY, bg);
                gfx.setTextSize(2);
                gfx.drawString("qty", UI::kContentX + UI::kContentW - 14, textY);
            }
        }
        y += lineHeight(line.isHeader);
    }
}

void ListScreen::drawAddBar(M5GFX& gfx) {
    gfx.fillRect(0, kAddBarY - 4, UI::kScreenW, kAddBarH + 4, TFT_WHITE);
    gfx.drawFastHLine(0, kAddBarY - 4, UI::kScreenW, TFT_BLACK);

    const int barH = kAddBarH - 8;
    const int addBtnW = UI::kContentW - kSettingsBtnW - kAddBarGap;
    const int settingsBtnX = UI::kContentX + addBtnW + kAddBarGap;

    UI::card(gfx, UI::kContentX, kAddBarY, addBtnW, barH);
    gfx.setTextColor(TFT_BLACK, TFT_WHITE);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextSize(3);
    gfx.drawString("+  ADD ITEM", UI::kContentX + addBtnW / 2, kAddBarY + barH / 2);

    UI::card(gfx, settingsBtnX, kAddBarY, kSettingsBtnW, barH);
    gfx.setTextColor(TFT_BLACK, TFT_WHITE);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextSize(2);
    gfx.drawString("SETTINGS", settingsBtnX + kSettingsBtnW / 2, kAddBarY + barH / 2);
}

void ListScreen::pushContent() {
    drawContent(M5.Display);
    Epd::getInstance().partialUpdate(0, kContentAreaY, UI::kScreenW, kContentAreaH);
}

void ListScreen::handleTouch(const TouchEvent& ev) {
    if (ev.type == TouchEventType::SwipeUp || ev.type == TouchEventType::SwipeDown) {
        // Swipe up reveals what's below, like any touchscreen list.
        page(ev.type == TouchEventType::SwipeUp ? 1 : -1);
        return;
    }
    if (ev.type != TouchEventType::Click) return;

    if (ev.y < UI::kHeaderY + UI::kHeaderH) {
        if (_openSettings) _openSettings();
        return;
    }

    if (ev.y >= kAddBarY - 4) {
        const int addBtnW = UI::kContentW - kSettingsBtnW - kAddBarGap;
        const int settingsBtnX = UI::kContentX + addBtnW + kAddBarGap;
        if (ev.x >= settingsBtnX) {
            if (_openSettings) _openSettings();
            return;
        }
        KeyboardWidget::getInstance().open(&_data->catalog, [this](const KeyboardResult& result) {
            // Opened from here rather than from main.cpp's touch dispatch: the keyboard has just
            // closed itself (see keyboard.cpp's SEND handler) but this callback still runs within
            // that same touch event, so QuantityScreen's own full-page open() is what actually
            // repaints the screen - main.cpp's overlayOpen() check sees the keyboard closed and
            // QuantityScreen open in the same pass and correctly skips its own redraw.
            const std::string name = result.text;
            const int categoryId = result.categoryId;
            QuantityScreen::getInstance().open(
                name, /*hasQuantity=*/false, 0.0f, "", /*isNewItem=*/true,
                [this, name, categoryId](const QuantityResult& qr) {
                    addItem(name, categoryId, qr.hasQuantity, qr.quantity, qr.unit);
                });
        });
        return;
    }

    int y = UI::kContentTop;
    for (size_t i = _scrollOffset; i < _lines.size(); ++i) {
        const Line& line = _lines[i];
        if (y + lineHeight(line.isHeader) > kContentBottom) break;
        if (!line.isHeader && ev.y >= y && ev.y < y + UI::kRowH) {
            const int qtyZoneX = UI::kContentX + UI::kContentW - kQtyZoneW;
            if (ev.x >= qtyZoneX) openQuantityEditor(line.itemIndex);
            else toggleItem(line.itemIndex);
            return;
        }
        y += lineHeight(line.isHeader);
    }
}

bool ListScreen::page(int direction) {
    // Approximate: a page is as many item rows as fit. Aisle headers are shorter, so a page can
    // overlap slightly - harmless.
    const int linesPerPage = (kContentBottom - UI::kContentTop) / kItemLineH;
    if (_lines.empty()) return false;

    const int newOffset = std::max(0, std::min((int)_lines.size() - 1, _scrollOffset + direction * linesPerPage));
    if (newOffset == _scrollOffset) return false;
    _scrollOffset = newOffset;
    pushContent();
    return true;
}

void ListScreen::toggleItem(int itemIndex) {
    Item& item = _data->items[itemIndex];
    if (item.isPending()) return; // no server id to send the change to yet

    item.purchased = !item.purchased;
    LocalStore::getInstance().queueToggle(item.id, item.purchased);
    ESP_LOGI(TAG, "Queued purchased=%d for item %d ('%s')", item.purchased, item.id, item.name.c_str());
    pushContent();

    if (_requestSync) _requestSync();
}

void ListScreen::openQuantityEditor(int itemIndex) {
    Item& item = _data->items[itemIndex];
    if (item.isPending()) return; // not yet confirmed by the server - nothing to edit yet
    const int itemId = item.id;

    QuantityScreen::getInstance().open(
        item.name.c_str(), item.hasQuantity, item.quantity, item.unit.c_str(), /*isNewItem=*/false,
        [this, itemId](const QuantityResult& qr) {
            for (auto& it : _data->items) {
                if (it.id != itemId) continue;
                it.hasQuantity = qr.hasQuantity;
                it.quantity = qr.quantity;
                it.unit = qr.unit.c_str();
                break;
            }
            LocalStore::getInstance().queueSetQuantity(itemId, qr.hasQuantity, qr.quantity, qr.unit.c_str());
            ESP_LOGI(TAG, "Queued quantity for item %d (hasQuantity=%d)", itemId, qr.hasQuantity);
            // No redraw here: main.cpp's post-touch overlayOpen() transition check repaints once
            // QuantityScreen closes.
            if (_requestSync) _requestSync();
        });
}

void ListScreen::addItem(const std::string& rawName, int categoryId, bool hasQuantity, float quantity,
                         const std::string& unit) {
    // The server rejects blank names too, but a queued add it rejects would just be dropped silently.
    const size_t start = rawName.find_first_not_of(" \t");
    if (start == std::string::npos) return;
    const size_t end = rawName.find_last_not_of(" \t");

    Item item;
    item.name = rawName.substr(start, end - start + 1).c_str();
    item.categoryId = categoryId;
    item.hasQuantity = hasQuantity;
    item.quantity = quantity;
    item.unit = unit.c_str();
    _data->items.push_back(item);
    LocalStore::getInstance().queueAdd(item.name, categoryId, hasQuantity, quantity, unit.c_str());
    ESP_LOGI(TAG, "Queued add '%s' (aisle hint %d)", item.name.c_str(), categoryId);

    // No redraw here: main.cpp's overlayOpen() transition check repaints once every overlay in this
    // chain (keyboard -> QuantityScreen) has closed.
    rebuildLines();

    if (_requestSync) _requestSync();
}

} // namespace ShoppingList
