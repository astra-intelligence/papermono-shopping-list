#pragma once

// On-screen keyboard with an autosuggest strip, drawn as an overlay over the bottom half of the
// screen.
//
// Suggestions come from the on-device copy of the catalog, so they work with Wi-Fi off. Picking a
// suggestion also carries its known aisle along, so the server doesn't have to classify it again.
//
// Refresh discipline: a keystroke pushes only the suggestion strip and input box to the panel; the
// whole overlay is pushed only on open, shift changes and picking a suggestion.
//
// LIFETIME: open() keeps a pointer to the caller's catalog vector, and the suggestion list points at
// its elements, until the keyboard closes. A sync replaces that vector, so main.cpp must not sync
// while isOpen() - that check is load-bearing, not dead code.

#include <functional>
#include <string>
#include <vector>

#include <M5Unified.h>

#include "hal/touch.h"
#include "model.h"

namespace ShoppingList {

struct KeyboardResult {
    std::string text;
    int categoryId = kNoCategory; // known aisle if the text is an exact catalog match
};

class KeyboardWidget {
public:
    static KeyboardWidget& getInstance() {
        static KeyboardWidget instance;
        return instance;
    }

    void open(const std::vector<CatalogEntry>* catalog, std::function<void(const KeyboardResult&)> onSend);
    void close() { _open = false; }
    bool isOpen() const { return _open; }

    // While open, the keyboard owns all touch input (including taps outside it, which close it).
    void handleTouch(const TouchEvent& ev);

private:
    KeyboardWidget() = default;

    enum class Shift : uint8_t { Off, Once, Lock };

    void draw(M5GFX& gfx);
    void drawSuggestions(M5GFX& gfx);
    void drawInputBox(M5GFX& gfx);
    void updateSuggestions();
    void typeChar(char c);
    void pushInputArea();
    void pushOverlay();

    bool _open = false;
    std::string _buffer;
    Shift _shift = Shift::Off;
    std::function<void(const KeyboardResult&)> _onSend;
    const std::vector<CatalogEntry>* _catalog = nullptr;
    std::vector<const CatalogEntry*> _suggestions;
};

} // namespace ShoppingList
