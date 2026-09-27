#pragma once

// Full-screen overlay for setting an item's quantity/unit - opened both right after the keyboard's
// SEND when adding a new item (before it's actually created; see list_screen.cpp) and by tapping the
// quantity zone on an existing item's row. Structured like settings_screen.h: a self-contained
// singleton with open()/close()/isOpen()/handleTouch(), reusing ui/widgets.h's row primitives.
//
// Units are a short fixed list picked from a popup, not free text - fast to tap on this
// touchscreen, at the cost that a custom unit set from the web app (e.g. "dozen") can't be
// reproduced here; editing that item's quantity from the device will only offer the fixed list.

#include <functional>
#include <string>

#include <M5Unified.h>

#include "hal/touch.h"

namespace ShoppingList {

struct QuantityResult {
    bool hasQuantity = false;
    float quantity = 0.0f;
    std::string unit; // empty = plain count (e.g. "x3")
};

class QuantityScreen {
public:
    static QuantityScreen& getInstance() {
        static QuantityScreen instance;
        return instance;
    }

    // `isNewItem` only changes the confirm button's label (ADD vs SAVE); starting values are
    // whatever the caller passes in (a new item starts with hasQuantity=false). CANCEL just closes
    // without calling onConfirm - for a new item that abandons the add entirely.
    void open(const std::string& itemName, bool hasQuantity, float quantity, const std::string& unit,
              bool isNewItem, std::function<void(const QuantityResult&)> onConfirm);
    void close() { _open = false; }
    bool isOpen() const { return _open; }

    void handleTouch(const TouchEvent& ev);

private:
    QuantityScreen() = default;

    void draw(M5GFX& gfx);

    bool _open = false;
    std::string _itemName;
    bool _isNewItem = false;
    int _quantitySteps = 0; // 0 = "None" (no quantity); whole-number steps of 1 above that
    int _unitIndex = 0;     // index into kUnitOptions (quantity_screen.cpp)
    bool _unitPickerOpen = false;
    std::function<void(const QuantityResult&)> _onConfirm;
};

} // namespace ShoppingList
