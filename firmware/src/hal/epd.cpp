#include "hal/epd.h"

#include <M5Unified.h>
#include <esp_log.h>

#include "config.h"

static constexpr const char* TAG = "EPD";

namespace ShoppingList {

void Epd::init() {
    M5.Display.setRotation(0);
    M5.Display.setAutoDisplay(false);
    M5.Display.fillScreen(TFT_WHITE);
    fullRefresh();
    ESP_LOGI(TAG, "EPD initialized: %dx%d", M5.Display.width(), M5.Display.height());
}

void Epd::partialUpdate(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > M5.Display.width()) w = M5.Display.width() - x;
    if (y + h > M5.Display.height()) h = M5.Display.height() - y;
    if (w <= 0 || h <= 0) return;

    if (++_partialCount >= Config::kMaxPartialRefreshes) {
        // The framebuffer already holds the new content, so a full refresh shows it too.
        fullRefresh();
        return;
    }
    M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    M5.Display.display(x, y, w, h);
}

void Epd::fullRefresh() {
    M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    M5.Display.display();
    M5.Display.waitDisplay();
    _partialCount = 0;
}

} // namespace ShoppingList
