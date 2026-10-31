// Checks the per-key LED maps: every key inside the matrix, no two keys on
// one LED (except where the hardware shares one), and the derived layouts
// trimmed and shifted the way they should be.

#include "ledmap.h"

#include <QSet>

#include <linux/input-event-codes.h>

#include <cstdio>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

RazerDevice keyboard(quint16 productId)
{
    RazerDevice d;
    d.kind = RazerDevice::Keyboard;
    d.productId = productId;
    return d;
}

// Every key in the matrix, and only the Huntsman Elite's volume dial shares
// an LED (mute, volume down and up all light its ring).
void checkSane(quint16 productId)
{
    const LedMap m = ledMap(keyboard(productId));
    CHECK(!m.isEmpty());
    QHash<QPoint, int> used;
    for (auto it = m.keys.cbegin(); it != m.keys.cend(); ++it) {
        const QPoint p = *it;
        if (p.x() < 0 || p.y() < 0 || p.x() >= m.columns || p.y() >= m.rows) {
            std::fprintf(stderr, "FAIL 0x%04x: key %d at (%d,%d) is outside %dx%d\n", productId, it.key(), p.x(),
                         p.y(), m.columns, m.rows);
            ++g_failures;
        }
        const bool dial = it.key() == KEY_MUTE || it.key() == KEY_VOLUMEDOWN || it.key() == KEY_VOLUMEUP;
        if (!dial && used.contains(p)) {
            std::fprintf(stderr, "FAIL 0x%04x: keys %d and %d share (%d,%d)\n", productId, it.key(), used[p], p.x(),
                         p.y());
            ++g_failures;
        }
        if (!dial)
            used.insert(p, it.key());
    }
}

} // namespace

int main()
{
    // Every model with a layout has a sane map.
    for (quint16 pid : {0x0226, 0x0203, 0x0211, 0x0214, 0x0216, 0x0217, 0x0221, 0x0228, 0x0237, 0x0241, 0x024E,
                        0x025A, 0x025C, 0x0293, 0x021E, 0x021F, 0x025D, 0x022A, 0x022C, 0x025E, 0x0227, 0x026C,
                        0x0266, 0x02A6, 0x02CF, 0x0295, 0x0292, 0x0290, 0x0209, 0x021A, 0x0A24, 0x02D7, 0x02D5,
                        0x0243, 0x026B, 0x02A7, 0x0257, 0x0282, 0x02B0}) {
        CHECK(keyboardLayout(keyboard(pid)) != KeyboardLayout::None);
        checkSane(pid);
    }

    // Unknown keyboards, and mice, have none.
    CHECK(ledMap(keyboard(0x0001)).isEmpty());
    CHECK(keyboardLayout(keyboard(0x0001)) == KeyboardLayout::None);
    RazerDevice mouse = keyboard(0x0257);
    mouse.kind = RazerDevice::Mouse;
    CHECK(ledMap(mouse).isEmpty());
    CHECK(keyboardLayout(mouse) == KeyboardLayout::None);

    // The Huntsman Elite: checked on the hardware.
    const LedMap elite = ledMap(keyboard(0x0226));
    CHECK(elite.rows == 9 && elite.columns == 22);
    CHECK(elite.keys.value(KEY_ESC) == QPoint(1, 0));
    CHECK(elite.keys.value(KEY_PLAYPAUSE) == QPoint(19, 0));
    CHECK(elite.keys.value(KEY_FN) == QPoint(12, 5));
    CHECK(!elite.edges.isEmpty());

    // Full size: the same main keys, without the media keys or light strips.
    const LedMap full = ledMap(keyboard(0x024E)); // BlackWidow V3
    CHECK(full.rows == 6 && full.columns == 22);
    CHECK(full.edges.isEmpty() && full.edgeRows.isEmpty());
    CHECK(!full.keys.contains(KEY_PLAYPAUSE) && !full.keys.contains(KEY_VOLUMEUP));
    for (auto it = full.keys.cbegin(); it != full.keys.cend(); ++it)
        CHECK(elite.keys.value(it.key()) == *it);
    // Spot checks against OpenRazer's KEY_MAPPING (row, col there).
    CHECK(full.keys.value(KEY_ESC) == QPoint(1, 0));
    CHECK(full.keys.value(KEY_F12) == QPoint(14, 0));
    CHECK(full.keys.value(KEY_GRAVE) == QPoint(1, 1));
    CHECK(full.keys.value(KEY_NUMLOCK) == QPoint(18, 1));
    CHECK(full.keys.value(KEY_Q) == QPoint(2, 2));
    CHECK(full.keys.value(KEY_ENTER) == QPoint(14, 3));
    CHECK(full.keys.value(KEY_KPENTER) == QPoint(21, 4));
    CHECK(full.keys.value(KEY_SPACE) == QPoint(7, 5));
    CHECK(full.keys.value(KEY_RIGHT) == QPoint(17, 5));
    CHECK(full.keys.value(KEY_KPDOT) == QPoint(20, 5));

    // The Huntsman V2 Analog's underglow rows are in the matrix, but unmapped.
    const LedMap analog = ledMap(keyboard(0x0266));
    CHECK(analog.rows == 8 && analog.keys.size() == full.keys.size());

    // BlackWidow Elite: space and right Alt a column to the left.
    const LedMap bwElite = ledMap(keyboard(0x0228));
    CHECK(bwElite.keys.value(KEY_SPACE) == QPoint(6, 5));
    CHECK(bwElite.keys.value(KEY_RIGHTALT) == QPoint(10, 5));

    // Tenkeyless: no numpad, the rest where it was, in either matrix size.
    for (quint16 pid : {0x0243, 0x02A7}) {
        const LedMap tkl = ledMap(keyboard(pid));
        CHECK(!tkl.keys.contains(KEY_NUMLOCK) && !tkl.keys.contains(KEY_KP0) && !tkl.keys.contains(KEY_KPENTER));
        CHECK(tkl.keys.value(KEY_PAUSE) == QPoint(17, 0));
        CHECK(tkl.keys.value(KEY_RIGHT) == QPoint(17, 5));
        CHECK(tkl.keys.value(KEY_SPACE) == QPoint(7, 5));
    }

    // 60%: no function row, navigation or numpad; up a row; Esc where ` was.
    const LedMap mini = ledMap(keyboard(0x0257));
    CHECK(mini.rows == 5 && mini.columns == 15);
    CHECK(mini.keys.value(KEY_ESC) == QPoint(1, 0));
    CHECK(!mini.keys.contains(KEY_GRAVE) && !mini.keys.contains(KEY_F1) && !mini.keys.contains(KEY_DELETE));
    CHECK(!mini.keys.contains(KEY_UP) && !mini.keys.contains(KEY_KP5));
    CHECK(mini.keys.value(KEY_1) == QPoint(2, 0));
    CHECK(mini.keys.value(KEY_BACKSPACE) == QPoint(14, 0));
    CHECK(mini.keys.value(KEY_Q) == QPoint(2, 1));
    CHECK(mini.keys.value(KEY_ENTER) == QPoint(14, 2));
    CHECK(mini.keys.value(KEY_Z) == QPoint(3, 3));
    CHECK(mini.keys.value(KEY_SPACE) == QPoint(7, 4));
    CHECK(mini.keys.value(KEY_FN) == QPoint(12, 4));
    CHECK(mini.keys.value(KEY_RIGHTCTRL) == QPoint(14, 4));

    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        std::printf("all LED map checks passed\n");
    return g_failures ? 1 : 0;
}
