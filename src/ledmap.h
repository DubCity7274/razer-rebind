#pragma once

#include "razerdevice.h"

#include <QHash>
#include <QList>
#include <QPoint>

// Which key layout a keyboard has, for its picture and its LED map.
// HuntsmanElite was checked on the hardware, with its media keys and light
// strips; the others are the same main keys, from OpenRazer's own key table,
// trimmed to the keyboard's size.
enum class KeyboardLayout { None, HuntsmanElite, FullSize, Tenkeyless, Mini };
KeyboardLayout keyboardLayout(const RazerDevice &device);
// Only HuntsmanElite has been tested on the real keyboard.
inline bool layoutVerified(KeyboardLayout l) { return l == KeyboardLayout::HuntsmanElite; }

// Where a keyboard's keys sit in its LED matrix, for per-key lighting.
struct LedMap
{
    int rows = 0;
    int columns = 0;
    QHash<int, QPoint> keys;  // evdev key code -> (column, row)
    QList<int> edgeRows;      // rows that light the edges (underglow, wrist rest)
    QHash<int, QPoint> edges; // Keys::edgeLed() code -> (column, row)

    bool isEmpty() const { return keys.isEmpty(); }
};

LedMap ledMap(const RazerDevice &device);
