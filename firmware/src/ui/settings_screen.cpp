#include "ui/settings_screen.h"

#include <algorithm>
#include <cstdio>

#include "hal/epd.h"
#include "ui/widgets.h"

namespace ShoppingList {
namespace {

constexpr uint8_t kFrontlightLevels[] = {0, 25, 60, 100};
constexpr const char* kFrontlightLabels[] = {"OFF", "LOW", "MED", "HIGH"};
constexpr int kFrontlightStepCount = 4;

enum Row { kFrontlight, kSyncNow, kWifi, kLastSync, kBattery, kClose, kRowCount };

void layout(UI::RowRect* rects) {
    static const UI::RowDef defs[kRowCount] = {
        {UI::RowKind::Stepper, "FRONTLIGHT"}, {UI::RowKind::Action, "SYNC NOW"},
        {UI::RowKind::Info, "WI-FI"},         {UI::RowKind::Info, "LAST SYNC"},
        {UI::RowKind::Info, "BATTERY"},       {UI::RowKind::Action, "CLOSE"},
    };
    UI::layoutRows(defs, kRowCount, rects);
}

// CLOSE is the one always-available way out, so it's styled to be unmissable rather than looking
// like another action row.
void drawCloseButton(M5GFX& gfx, const UI::RowRect& r) {
    gfx.fillRoundRect(UI::kContentX, r.y, UI::kContentW, r.h, UI::kCorner, TFT_BLACK);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextSize(3);
    gfx.drawString("CLOSE", UI::kContentX + UI::kContentW / 2, r.y + r.h / 2);
}

} // namespace

void SettingsScreen::open(const SettingsSnapshot& snapshot, std::function<void(uint8_t)> onFrontlightChange,
                          std::function<void()> onSyncNow) {
    _open = true;
    _snapshot = snapshot;
    _onFrontlightChange = std::move(onFrontlightChange);
    _onSyncNow = std::move(onSyncNow);

    _frontlightStep = 0;
    for (int i = kFrontlightStepCount - 1; i >= 0; --i) {
        if (snapshot.frontlightPercent >= kFrontlightLevels[i]) {
            _frontlightStep = i;
            break;
        }
    }

    // A fast partial rather than the full anti-ghosting waveform: this is a plain text screen, and a
    // full-panel flash every time someone just wants to check the battery/WiFi status is annoying.
    // Closing back to the shopping list still goes through showListFull() (see main.cpp) - that
    // screen has more visual weight and is worth refreshing properly.
    draw(M5.Display);
    Epd::getInstance().partialUpdate(0, 0, UI::kScreenW, UI::kScreenH);
}

void SettingsScreen::draw(M5GFX& gfx) {
    UI::RowRect rects[kRowCount];
    layout(rects);

    gfx.fillScreen(TFT_WHITE);
    UI::header(gfx, "SETTINGS");

    UI::stepperRow(gfx, rects[kFrontlight], "FRONTLIGHT", kFrontlightLabels[_frontlightStep], _frontlightStep > 0,
                   _frontlightStep < kFrontlightStepCount - 1);
    UI::actionRow(gfx, rects[kSyncNow], "SYNC NOW");
    UI::infoRow(gfx, rects[kWifi], "WI-FI", _snapshot.wifiSsid.c_str());

    char syncText[32];
    if (!_snapshot.everSynced) {
        snprintf(syncText, sizeof(syncText), "never");
    } else {
        const char* result = _snapshot.lastSyncOk ? "OK" : "FAILED";
        const uint32_t ago = _snapshot.lastSyncAgoSec;
        if (ago < 60) snprintf(syncText, sizeof(syncText), "%s %us ago", result, (unsigned)ago);
        else snprintf(syncText, sizeof(syncText), "%s %um ago", result, (unsigned)(ago / 60));
    }
    UI::infoRow(gfx, rects[kLastSync], "LAST SYNC", syncText);

    char batteryText[16];
    if (_snapshot.batteryPercent < 0) snprintf(batteryText, sizeof(batteryText), "--");
    else snprintf(batteryText, sizeof(batteryText), "%d%%", _snapshot.batteryPercent);
    UI::infoRow(gfx, rects[kBattery], "BATTERY", batteryText);

    drawCloseButton(gfx, rects[kClose]);
}

void SettingsScreen::handleTouch(const TouchEvent& ev) {
    if (!_open) return;
    // Swipe right also closes, for anyone used to it as "back".
    if (ev.type == TouchEventType::SwipeRight) {
        close();
        return;
    }
    if (ev.type != TouchEventType::Click) return;

    UI::RowRect rects[kRowCount];
    layout(rects);

    if (UI::inRow(ev, rects[kClose])) {
        close();
    } else if (UI::inRow(ev, rects[kSyncNow])) {
        if (_onSyncNow) _onSyncNow();
        close();
    } else if (UI::inRow(ev, rects[kFrontlight])) {
        const UI::RowRect& r = rects[kFrontlight];
        int step = _frontlightStep;
        if (UI::stepperHitMinus(ev, r)) step = std::max(0, step - 1);
        else if (UI::stepperHitPlus(ev, r)) step = std::min(kFrontlightStepCount - 1, step + 1);
        if (step == _frontlightStep) return;

        _frontlightStep = step;
        if (_onFrontlightChange) _onFrontlightChange(kFrontlightLevels[step]);
        draw(M5.Display);
        Epd::getInstance().partialUpdate(UI::kContentX, r.y, UI::kContentW, r.h);
    }
}

} // namespace ShoppingList
