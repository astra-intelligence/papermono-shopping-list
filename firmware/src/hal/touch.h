#pragma once

// FT6336G touch input, turned into one gesture event per loop iteration (click, swipe, ...).
// Carried over unchanged from MonoMesh, where it's proven on this board.

#include <Arduino.h>
#include <M5Unified.h>

namespace ShoppingList {

enum class TouchEventType {
    None,
    Down,
    Move,
    Up,
    Click,
    SwipeLeft,
    SwipeRight,
    SwipeDown,
    SwipeUp
};

struct TouchEvent {
    TouchEventType type = TouchEventType::None;
    int16_t x = 0;
    int16_t y = 0;
    int16_t startX = 0;
    int16_t startY = 0;
    uint32_t durationMs = 0;
};

class TouchManager {
public:
    static TouchManager& getInstance() {
        static TouchManager instance;
        return instance;
    }

    bool init();
    void update();

    bool hasEvent() const { return _currentEvent.type != TouchEventType::None; }
    TouchEvent popEvent();

private:
    TouchManager() = default;
    ~TouchManager() = default;

    bool _isPressed = false;
    int16_t _curX = 0;
    int16_t _curY = 0;
    int16_t _downX = 0;
    int16_t _downY = 0;
    uint32_t _downTime = 0;
    uint32_t _lastM5PollMs = 0;   // keep-alive cadence for M5.update() when the panel is idle
    uint32_t _lastIntLowMs = 0;   // last time TOUCH_INT was seen asserted

    TouchEvent _currentEvent;
};

} // namespace ShoppingList
