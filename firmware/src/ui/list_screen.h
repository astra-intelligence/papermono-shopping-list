#pragma once

// The main screen: the list grouped by aisle (the server sends items already in walking order), a
// header showing sync freshness (tap it for settings), and an "add item" bar that opens the keyboard.
// Tap an item to tick it off. Swipe up/down, or the two side buttons, to page.

#include <functional>
#include <string>
#include <vector>

#include <M5Unified.h>

#include "hal/touch.h"
#include "model.h"

namespace ShoppingList {

class ListScreen {
public:
    // `data` is owned by main.cpp and updated in place by syncs; call onDataChanged() afterwards.
    void bind(ShoppingData* data);
    void onDataChanged();

    // Fired after a local edit, so main.cpp can sync it sooner than the next periodic sync.
    void setSyncRequestCallback(std::function<void()> cb) { _requestSync = std::move(cb); }
    // Called when Settings is requested - the SETTINGS button in the add bar (see drawAddBar()) is
    // the discoverable way in; tapping the header still works too, kept as a shortcut.
    void setOpenSettingsCallback(std::function<void()> cb) { _openSettings = std::move(cb); }

    // Draws the whole screen into the framebuffer (the caller decides how to push it to the panel).
    void draw(M5GFX& gfx);
    void handleTouch(const TouchEvent& ev);
    // direction: -1 = towards the top, +1 = towards the end. Returns false if already at that end.
    // On success the new page has been drawn and pushed.
    bool page(int direction);

    // Called from main.cpp right before/after the blocking sync call, so the header can show
    // "syncing..." for however long the Wi-Fi connect + HTTP round trip takes - a tap-triggered sync
    // (see Config::kTapSyncStaleMs) would otherwise look like the tap did nothing for several
    // seconds. Only meaningful while this screen is the one on screen, which main.cpp guarantees:
    // maybeSync() never runs a sync while an overlay is open. Repaints just the header, not the
    // whole screen.
    void setSyncing(bool syncing);

private:
    struct Line {
        bool isHeader;
        String text;        // aisle name for headers, item name otherwise
        int itemIndex;      // into _data->items; -1 for headers
    };

    void rebuildLines();
    void drawHeader(M5GFX& gfx);
    void drawContent(M5GFX& gfx);
    void drawAddBar(M5GFX& gfx);
    void pushContent();
    void toggleItem(int itemIndex);
    void openQuantityEditor(int itemIndex);
    void addItem(const std::string& name, int categoryId, bool hasQuantity, float quantity,
                 const std::string& unit);

    ShoppingData* _data = nullptr;
    std::function<void()> _requestSync;
    std::function<void()> _openSettings;

    std::vector<Line> _lines;
    int _scrollOffset = 0; // index into _lines of the first visible line
    bool _syncing = false;
};

} // namespace ShoppingList
