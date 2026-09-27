#include "ui/quantity_screen.h"

#include <cstdio>

#include "hal/epd.h"
#include "ui/widgets.h"

namespace ShoppingList {
namespace {

// kUnitOptions[0] = "" (plain count, "x3") is a real, selectable choice, not a placeholder.
// kUnitLabels mirrors it 1:1 for display, spelling the empty option out as "none" so the UNIT row
// and popup never show a blank value.
constexpr const char* kUnitOptions[] = {"", "pcs", "g", "kg", "ml", "L", "pack", "box"};
constexpr const char* kUnitLabels[] = {"none", "pcs", "g", "kg", "ml", "L", "pack", "box"};
constexpr int kUnitCount = 8;
constexpr int kMaxQuantity = 99;

// Popup listing every unit option, opened by tapping the UNIT row. Positioned to drop down from
// that row rather than a fixed screen offset, so it stays sensible if the row layout above it ever
// changes.
constexpr int kUnitPopupRowH = 44;
constexpr int kUnitPopupGap = 6;
constexpr int kUnitPopupPad = 12;
constexpr int kUnitPopupTitleH = 36;

enum Row { kItem, kQuantity, kUnit, kConfirm, kRowCount };

void layout(UI::RowRect* rects) {
    static const UI::RowDef defs[kRowCount] = {
        {UI::RowKind::Info, "ITEM"},
        {UI::RowKind::Stepper, "QUANTITY"},
        {UI::RowKind::Info, "UNIT"},
        {UI::RowKind::Action, "CONFIRM"},
    };
    UI::layoutRows(defs, kRowCount, rects);
}

// UNIT looks like an info row but is tappable (opens the unit popup) whenever a quantity is set -
// infoRow's fixed light-gray fill would misleadingly suggest it never does anything.
void drawUnitRow(M5GFX& gfx, const UI::RowRect& r, const char* value, bool enabled) {
    const uint16_t bg = enabled ? TFT_WHITE : TFT_LIGHTGRAY;
    UI::card(gfx, UI::kContentX, r.y, UI::kContentW, r.h, bg);
    gfx.setTextColor(TFT_BLACK, bg);
    gfx.setTextDatum(textdatum_t::middle_left);
    gfx.setTextSize(2);
    gfx.drawString("UNIT", UI::kContentX + 16, r.y + r.h / 2);
    gfx.setTextDatum(textdatum_t::middle_right);
    if (gfx.textWidth(value) > 250) gfx.setTextSize(1);
    gfx.drawString(value, UI::kContentX + UI::kContentW - 16, r.y + r.h / 2);
}

int unitPopupY(const UI::RowRect& unitRow) { return unitRow.y + unitRow.h + 10; }

void drawUnitPopup(M5GFX& gfx, int popupY, int selectedIndex) {
    const int boxH = kUnitPopupTitleH + kUnitCount * kUnitPopupRowH + (kUnitCount - 1) * kUnitPopupGap +
                     2 * kUnitPopupPad;
    UI::card(gfx, UI::kContentX, popupY, UI::kContentW, boxH, TFT_WHITE);
    gfx.setTextColor(TFT_BLACK, TFT_WHITE);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextSize(2);
    gfx.drawString("UNIT", UI::kContentX + UI::kContentW / 2, popupY + kUnitPopupTitleH / 2);

    const int rowX = UI::kContentX + kUnitPopupPad;
    const int rowW = UI::kContentW - 2 * kUnitPopupPad;
    int y = popupY + kUnitPopupTitleH + kUnitPopupPad;
    for (int i = 0; i < kUnitCount; ++i) {
        const bool selected = i == selectedIndex;
        gfx.fillRoundRect(rowX, y, rowW, kUnitPopupRowH, 4, selected ? TFT_BLACK : TFT_WHITE);
        gfx.drawRoundRect(rowX, y, rowW, kUnitPopupRowH, 4, TFT_BLACK);
        gfx.setTextColor(selected ? TFT_WHITE : TFT_BLACK, selected ? TFT_BLACK : TFT_WHITE);
        gfx.setTextDatum(textdatum_t::middle_center);
        gfx.setTextSize(2);
        gfx.drawString(kUnitLabels[i], rowX + rowW / 2, y + kUnitPopupRowH / 2);
        y += kUnitPopupRowH + kUnitPopupGap;
    }
}

int hitUnitPopupRow(const TouchEvent& ev, int popupY) {
    const int rowX = UI::kContentX + kUnitPopupPad;
    const int rowW = UI::kContentW - 2 * kUnitPopupPad;
    int y = popupY + kUnitPopupTitleH + kUnitPopupPad;
    for (int i = 0; i < kUnitCount; ++i) {
        if (UI::inRect(ev, rowX, y, rowW, kUnitPopupRowH)) return i;
        y += kUnitPopupRowH + kUnitPopupGap;
    }
    return -1;
}

// Two side-by-side buttons - none of widgets.h's row kinds draw a Cancel/Confirm pair, so this is
// drawn locally rather than adding a one-off primitive to the shared widget file.
void drawConfirmRow(M5GFX& gfx, const UI::RowRect& r, const char* confirmLabel) {
    const int gap = 12;
    const int bw = (UI::kContentW - gap) / 2;
    const int cancelX = UI::kContentX;
    const int confirmX = UI::kContentX + bw + gap;

    gfx.drawRoundRect(cancelX, r.y, bw, r.h, UI::kCorner, TFT_BLACK);
    gfx.fillRoundRect(cancelX + 1, r.y + 1, bw - 2, r.h - 2, UI::kCorner - 1, TFT_WHITE);
    gfx.setTextColor(TFT_BLACK, TFT_WHITE);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextSize(2);
    gfx.drawString("CANCEL", cancelX + bw / 2, r.y + r.h / 2);

    gfx.fillRoundRect(confirmX, r.y, bw, r.h, UI::kCorner, TFT_BLACK);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.drawString(confirmLabel, confirmX + bw / 2, r.y + r.h / 2);
}

bool hitCancelButton(const TouchEvent& ev, const UI::RowRect& r) {
    const int bw = (UI::kContentW - 12) / 2;
    return UI::inRect(ev, UI::kContentX, r.y, bw, r.h);
}

bool hitConfirmButton(const TouchEvent& ev, const UI::RowRect& r) {
    const int gap = 12;
    const int bw = (UI::kContentW - gap) / 2;
    const int confirmX = UI::kContentX + bw + gap;
    return UI::inRect(ev, confirmX, r.y, bw, r.h);
}

} // namespace

void QuantityScreen::open(const std::string& itemName, bool hasQuantity, float quantity,
                          const std::string& unit, bool isNewItem,
                          std::function<void(const QuantityResult&)> onConfirm) {
    _open = true;
    _itemName = itemName;
    _isNewItem = isNewItem;
    _onConfirm = std::move(onConfirm);

    _quantitySteps = hasQuantity ? (int)(quantity + 0.5f) : 0;
    if (_quantitySteps < 0) _quantitySteps = 0;
    if (_quantitySteps > kMaxQuantity) _quantitySteps = kMaxQuantity;

    _unitIndex = 0;
    for (int i = 0; i < kUnitCount; ++i) {
        if (unit == kUnitOptions[i]) {
            _unitIndex = i;
            break;
        }
    }

    draw(M5.Display);
    Epd::getInstance().fullRefresh();
}

void QuantityScreen::draw(M5GFX& gfx) {
    UI::RowRect rects[kRowCount];
    layout(rects);

    gfx.fillScreen(TFT_WHITE);
    UI::header(gfx, "QUANTITY");

    UI::infoRow(gfx, rects[kItem], "ITEM", _itemName.c_str());

    char qtyVal[16];
    if (_quantitySteps <= 0) snprintf(qtyVal, sizeof(qtyVal), "None");
    else snprintf(qtyVal, sizeof(qtyVal), "%d", _quantitySteps);
    UI::stepperRow(gfx, rects[kQuantity], "QUANTITY", qtyVal, _quantitySteps > 0,
                   _quantitySteps < kMaxQuantity);

    const bool unitEnabled = _quantitySteps > 0;
    drawUnitRow(gfx, rects[kUnit], unitEnabled ? kUnitLabels[_unitIndex] : "-", unitEnabled);

    drawConfirmRow(gfx, rects[kConfirm], _isNewItem ? "ADD" : "SAVE");

    if (_unitPickerOpen) drawUnitPopup(gfx, unitPopupY(rects[kUnit]), _unitIndex);
}

void QuantityScreen::handleTouch(const TouchEvent& ev) {
    if (!_open) return;
    if (ev.type == TouchEventType::SwipeRight) {
        close();
        return;
    }
    if (ev.type != TouchEventType::Click) return;

    UI::RowRect rects[kRowCount];
    layout(rects);

    if (_unitPickerOpen) {
        // Any tap closes it - select the tapped unit if it hit a row, otherwise dismiss unchanged
        // (tap-outside-to-close).
        const int hit = hitUnitPopupRow(ev, unitPopupY(rects[kUnit]));
        if (hit >= 0) _unitIndex = hit;
        _unitPickerOpen = false;
        draw(M5.Display);
        Epd::getInstance().partialUpdate(0, 0, UI::kScreenW, UI::kScreenH);
        return;
    }

    if (UI::inRow(ev, rects[kQuantity])) {
        bool changed = false;
        if (UI::stepperHitMinus(ev, rects[kQuantity]) && _quantitySteps > 0) {
            --_quantitySteps;
            if (_quantitySteps == 0) _unitIndex = 0; // no quantity -> unit is meaningless too
            changed = true;
        } else if (UI::stepperHitPlus(ev, rects[kQuantity]) && _quantitySteps < kMaxQuantity) {
            ++_quantitySteps;
            changed = true;
        }
        if (changed) {
            draw(M5.Display);
            Epd::getInstance().partialUpdate(UI::kContentX, rects[kQuantity].y, UI::kContentW,
                                             (rects[kUnit].y + rects[kUnit].h) - rects[kQuantity].y);
        }
        return;
    }

    if (UI::inRow(ev, rects[kUnit]) && _quantitySteps > 0) {
        _unitPickerOpen = true;
        draw(M5.Display);
        Epd::getInstance().partialUpdate(0, 0, UI::kScreenW, UI::kScreenH);
        return;
    }

    if (UI::inRow(ev, rects[kConfirm])) {
        if (hitConfirmButton(ev, rects[kConfirm])) {
            QuantityResult result;
            result.hasQuantity = _quantitySteps > 0;
            result.quantity = (float)_quantitySteps;
            result.unit = result.hasQuantity ? kUnitOptions[_unitIndex] : "";
            auto onConfirm = _onConfirm;
            close();
            if (onConfirm) onConfirm(result);
        } else if (hitCancelButton(ev, rects[kConfirm])) {
            close();
        }
    }
}

} // namespace ShoppingList
