#pragma once

// Drawing and hit-testing primitives shared by the screens.
//
// Anything tappable is laid out once (as RowRects from layoutRows) and both the draw code and the
// touch code read the same rects, so the two can't drift apart. Conventions: interactive labels are
// text size 2, and every tap target is at least 44 px tall.
//
// The visual style (cards, dot-screen header) comes from MonoMesh's settings_widgets.h.

#include <M5Unified.h>

#include "hal/touch.h"

namespace ShoppingList {
namespace UI {

// ---------------------------------------------------------------- geometry (480 x 800 portrait)
constexpr int kScreenW = 480;
constexpr int kScreenH = 800;

// A little top margin so the header bar isn't flush against the physical edge of the panel.
constexpr int kHeaderY = 14;
constexpr int kHeaderH = 32;
constexpr int kContentTop = kHeaderY + kHeaderH + 6;

constexpr int kContentX = 14;
constexpr int kContentW = 452;
constexpr int kGap = 6;
constexpr int kRowH = 56;
constexpr int kCorner = 6;
constexpr int kMaxRows = 12;

// ---------------------------------------------------------------- hit testing
inline bool inRect(const TouchEvent& ev, int x, int y, int w, int h) {
    return ev.x >= x && ev.x < x + w && ev.y >= y && ev.y < y + h;
}

enum class RowKind : uint8_t {
    Info,    // read-only: label left, value right, light gray
    Stepper, // label left, [-] value [+] right
    Action,  // full-width button
};

struct RowDef {
    RowKind kind;
    const char* label;
};

struct RowRect {
    int y = 0;
    int h = kRowH;
};

// Stacks rows top to bottom from `top`, filling `out` with each row's rect.
inline void layoutRows(const RowDef* defs, int n, RowRect* out, int top = kContentTop) {
    int y = top;
    for (int i = 0; i < n && i < kMaxRows; ++i) {
        out[i].y = y;
        out[i].h = kRowH;
        y += out[i].h + kGap;
    }
}

inline bool inRow(const TouchEvent& ev, const RowRect& r) {
    return inRect(ev, kContentX, r.y, kContentW, r.h);
}

// Stepper controls, right-aligned: [-] [value] [+]
constexpr int kStepperBtnW = 52;
constexpr int kStepperBtnH = 44;
constexpr int kStepperValueW = 140;
constexpr int kStepperPlusX = kContentX + kContentW - 14 - kStepperBtnW;
constexpr int kStepperValueX = kStepperPlusX - 6 - kStepperValueW;
constexpr int kStepperMinusX = kStepperValueX - 6 - kStepperBtnW;

inline bool stepperHitMinus(const TouchEvent& ev, const RowRect& r) {
    return inRect(ev, kStepperMinusX, r.y + (r.h - kStepperBtnH) / 2, kStepperBtnW, kStepperBtnH);
}
inline bool stepperHitPlus(const TouchEvent& ev, const RowRect& r) {
    return inRect(ev, kStepperPlusX, r.y + (r.h - kStepperBtnH) / 2, kStepperBtnW, kStepperBtnH);
}

// ---------------------------------------------------------------- drawing
inline void card(M5GFX& g, int x, int y, int w, int h, uint16_t fill = TFT_WHITE) {
    g.drawRoundRect(x, y, w, h, kCorner, TFT_BLACK);
    g.fillRoundRect(x + 1, y + 1, w - 2, h - 2, kCorner - 1, fill);
}

// Header bars use a 1-bit dot screen rather than a gray fill: the fast partial waveform pushes
// mid-grays to black or white, so a gray bar looked different after every partial update until the
// next full refresh. Pure black/white pixels render the same under both waveforms.
inline void dotScreen(M5GFX& g, int y, int h) {
    g.fillRect(0, y, kScreenW, h, TFT_WHITE);
    for (int row = 0; row < h; ++row) {
        for (int x = (4 - (row & 3)) & 3; x < kScreenW; x += 4) g.drawPixel(x, y + row, TFT_BLACK);
    }
}

// textSize defaults to 2 (used by Settings and the quantity screen); the shopping list's header
// passes 3 - it's the only title on screen there and has room to spare, so it reads at a glance.
inline void header(M5GFX& g, const char* title, int textSize = 2) {
    dotScreen(g, kHeaderY, kHeaderH);
    g.drawFastHLine(0, kHeaderY + kHeaderH, kScreenW, TFT_BLACK);
    g.setTextColor(TFT_BLACK);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextSize(textSize);
    g.drawString(title, kContentX + 4, kHeaderY + kHeaderH / 2);
}

// Read-only row: light gray so it's obvious nothing happens on tap.
inline void infoRow(M5GFX& g, const RowRect& r, const char* label, const char* value) {
    card(g, kContentX, r.y, kContentW, r.h, TFT_LIGHTGRAY);
    g.setTextColor(TFT_BLACK, TFT_LIGHTGRAY);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextSize(2);
    g.drawString(label, kContentX + 16, r.y + r.h / 2);
    g.setTextDatum(textdatum_t::middle_right);
    if (g.textWidth(value) > 250) g.setTextSize(1);
    g.drawString(value, kContentX + kContentW - 16, r.y + r.h / 2);
}

inline void stepperRow(M5GFX& g, const RowRect& r, const char* label, const char* value, bool canDown,
                       bool canUp) {
    card(g, kContentX, r.y, kContentW, r.h);
    g.setTextColor(TFT_BLACK, TFT_WHITE);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextSize(2);
    g.drawString(label, kContentX + 16, r.y + r.h / 2);

    const int by = r.y + (r.h - kStepperBtnH) / 2;
    auto button = [&](int x, const char* glyph, bool enabled) {
        const uint16_t bg = enabled ? TFT_LIGHTGRAY : TFT_WHITE;
        g.drawRoundRect(x, by, kStepperBtnW, kStepperBtnH, 4, TFT_BLACK);
        g.fillRoundRect(x + 1, by + 1, kStepperBtnW - 2, kStepperBtnH - 2, 3, bg);
        g.setTextColor(enabled ? TFT_BLACK : TFT_LIGHTGRAY, bg);
        g.setTextDatum(textdatum_t::middle_center);
        g.setTextSize(3);
        g.drawString(glyph, x + kStepperBtnW / 2, by + kStepperBtnH / 2);
    };
    button(kStepperMinusX, "-", canDown);
    button(kStepperPlusX, "+", canUp);

    g.setTextColor(TFT_BLACK, TFT_WHITE);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextSize(2);
    g.drawString(value, kStepperValueX + kStepperValueW / 2, r.y + r.h / 2);
}

inline void actionRow(M5GFX& g, const RowRect& r, const char* label) {
    card(g, kContentX, r.y, kContentW, r.h);
    g.setTextColor(TFT_BLACK, TFT_WHITE);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextSize(2);
    g.drawString(label, kContentX + kContentW / 2, r.y + r.h / 2);
}

} // namespace UI
} // namespace ShoppingList
