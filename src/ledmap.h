#pragma once

#include "razerdevice.h"

#include <QHash>
#include <QList>
#include <QPoint>

// Where a keyboard's keys sit in its LED matrix, for per-key lighting.
// Only keyboards checked on real hardware have a map.
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
