#include "ledmap.h"

#include "keys.h"

#include <linux/input-event-codes.h>

namespace {

// Razer Huntsman Elite: a 22 x 9 matrix. Rows 0-5 are the keys (ANSI;
// KEY_102ND is the extra ISO key), rows 6-8 the light strips around the
// keyboard and the wrist rest. Checked on the hardware: the three round media
// buttons are columns 18-20 of row 0 (lit from between them), the volume
// dial's ring is column 21. Fn has an LED too, though the key itself never
// reaches applications.
LedMap huntsmanElite()
{
    LedMap m;
    m.rows = 9;
    m.columns = 22;
    m.edgeRows = {6, 7, 8};
    // 19 + 19 + 20 LEDs in the strips around the keyboard and the wrist rest.
    for (const auto &[r, count] : {std::pair{6, 19}, std::pair{7, 19}, std::pair{8, 20}})
        for (int c = 0; c < count; ++c)
            m.edges.insert(Keys::edgeLed(r, c), QPoint(c, r));
    auto row = [&m](int r, std::initializer_list<std::pair<int, int>> keys) {
        for (const auto &[col, code] : keys)
            m.keys.insert(code, QPoint(col, r));
    };
    row(0, {{1, KEY_ESC}, {3, KEY_F1}, {4, KEY_F2}, {5, KEY_F3}, {6, KEY_F4}, {7, KEY_F5}, {8, KEY_F6}, {9, KEY_F7},
            {10, KEY_F8}, {11, KEY_F9}, {12, KEY_F10}, {13, KEY_F11}, {14, KEY_F12}, {15, KEY_SYSRQ},
            {16, KEY_SCROLLLOCK}, {17, KEY_PAUSE}, {18, KEY_PREVIOUSSONG}, {19, KEY_PLAYPAUSE}, {20, KEY_NEXTSONG},
            {21, KEY_MUTE}, {21, KEY_VOLUMEDOWN}, {21, KEY_VOLUMEUP}});
    row(1, {{1, KEY_GRAVE}, {2, KEY_1}, {3, KEY_2}, {4, KEY_3}, {5, KEY_4}, {6, KEY_5}, {7, KEY_6}, {8, KEY_7},
            {9, KEY_8}, {10, KEY_9}, {11, KEY_0}, {12, KEY_MINUS}, {13, KEY_EQUAL}, {14, KEY_BACKSPACE},
            {15, KEY_INSERT}, {16, KEY_HOME}, {17, KEY_PAGEUP}, {18, KEY_NUMLOCK}, {19, KEY_KPSLASH},
            {20, KEY_KPASTERISK}, {21, KEY_KPMINUS}});
    row(2, {{1, KEY_TAB}, {2, KEY_Q}, {3, KEY_W}, {4, KEY_E}, {5, KEY_R}, {6, KEY_T}, {7, KEY_Y}, {8, KEY_U},
            {9, KEY_I}, {10, KEY_O}, {11, KEY_P}, {12, KEY_LEFTBRACE}, {13, KEY_RIGHTBRACE}, {14, KEY_BACKSLASH},
            {15, KEY_DELETE}, {16, KEY_END}, {17, KEY_PAGEDOWN}, {18, KEY_KP7}, {19, KEY_KP8}, {20, KEY_KP9},
            {21, KEY_KPPLUS}});
    row(3, {{1, KEY_CAPSLOCK}, {2, KEY_A}, {3, KEY_S}, {4, KEY_D}, {5, KEY_F}, {6, KEY_G}, {7, KEY_H}, {8, KEY_J},
            {9, KEY_K}, {10, KEY_L}, {11, KEY_SEMICOLON}, {12, KEY_APOSTROPHE}, {14, KEY_ENTER}, {18, KEY_KP4},
            {19, KEY_KP5}, {20, KEY_KP6}});
    row(4, {{1, KEY_LEFTSHIFT}, {2, KEY_102ND}, {3, KEY_Z}, {4, KEY_X}, {5, KEY_C}, {6, KEY_V}, {7, KEY_B},
            {8, KEY_N}, {9, KEY_M}, {10, KEY_COMMA}, {11, KEY_DOT}, {12, KEY_SLASH}, {14, KEY_RIGHTSHIFT},
            {16, KEY_UP}, {18, KEY_KP1}, {19, KEY_KP2}, {20, KEY_KP3}, {21, KEY_KPENTER}});
    row(5, {{1, KEY_LEFTCTRL}, {2, KEY_LEFTMETA}, {3, KEY_LEFTALT}, {7, KEY_SPACE}, {11, KEY_RIGHTALT},
            {12, KEY_FN}, {13, KEY_COMPOSE}, {14, KEY_RIGHTCTRL}, {15, KEY_LEFT}, {16, KEY_DOWN}, {17, KEY_RIGHT}, {19, KEY_KP0},
            {20, KEY_KPDOT}});
    return m;
}

} // namespace

LedMap ledMap(const RazerDevice &device)
{
    if (device.kind == RazerDevice::Keyboard && device.productId == 0x0226)
        return huntsmanElite();
    return {};
}
