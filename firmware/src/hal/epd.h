#pragma once

// E-paper refresh policy for the SSD1677 panel, on top of M5GFX.
//
// Two operations:
//  - partialUpdate(): fast 1-bit waveform for just a rectangle. Low contrast and no flash; used for
//    every interactive change (ticking an item, scrolling, typing).
//  - fullRefresh(): the high-quality waveform for the whole panel. Flashes for about a second, clears
//    ghosting and gives real black text. Used when content worth reading changes (boot, a sync that
//    changed something, closing an overlay).
//
// Partial updates are counted and a full refresh is forced after Config::kMaxPartialRefreshes, so
// the panel never runs an unbounded string of partial updates.

#include <cstdint>

namespace ShoppingList {

class Epd {
public:
    static Epd& getInstance() {
        static Epd instance;
        return instance;
    }

    void init();
    void partialUpdate(int32_t x, int32_t y, int32_t w, int32_t h);
    void fullRefresh();

private:
    Epd() = default;

    uint16_t _partialCount = 0;
};

} // namespace ShoppingList
