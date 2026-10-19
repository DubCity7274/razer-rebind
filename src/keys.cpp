#include "keys.h"

#include <QStringList>
#include <libevdev/libevdev.h>

namespace Keys {

static bool inRange(int c, int lo, int hi) { return c >= lo && c <= hi; }

QString name(int code)
{
    if (code == WheelUp)
        return QStringLiteral("WHEEL_UP");
    if (code == WheelDown)
        return QStringLiteral("WHEEL_DOWN");
    if (code == Hypershift)
        return QStringLiteral("HYPERSHIFT");
    if (const char *n = libevdev_event_code_get_name(EV_KEY, code))
        return QString::fromLatin1(n);
    return QStringLiteral("KEY_%1").arg(code);
}

int fromName(const QString &name)
{
    if (name == QLatin1String("WHEEL_UP"))
        return WheelUp;
    if (name == QLatin1String("WHEEL_DOWN"))
        return WheelDown;
    if (name == QLatin1String("HYPERSHIFT"))
        return Hypershift;
    const int code = libevdev_event_code_from_name(EV_KEY, name.toLatin1().constData());
    if (code >= 0)
        return code;
    if (name.startsWith(QLatin1String("KEY_"))) {
        bool ok = false;
        const int n = name.mid(4).toInt(&ok);
        if (ok && n > 0 && n <= KEY_MAX)
            return n;
    }
    return -1;
}

static const QHash<int, QString> &prettyNames()
{
    static const QHash<int, QString> names = {
        {KEY_ESC, "Esc"}, {KEY_BACKSPACE, "Backspace"}, {KEY_TAB, "Tab"},
        {KEY_ENTER, "Enter"}, {KEY_SPACE, "Space"}, {KEY_CAPSLOCK, "Caps Lock"},
        {KEY_LEFTCTRL, "L Ctrl"}, {KEY_RIGHTCTRL, "R Ctrl"},
        {KEY_LEFTSHIFT, "L Shift"}, {KEY_RIGHTSHIFT, "R Shift"},
        {KEY_LEFTALT, "L Alt"}, {KEY_RIGHTALT, "R Alt"},
        {KEY_LEFTMETA, "L Super"}, {KEY_RIGHTMETA, "R Super"}, {KEY_COMPOSE, "Menu"},
        {KEY_GRAVE, "`"}, {KEY_MINUS, "-"}, {KEY_EQUAL, "="},
        {KEY_LEFTBRACE, "["}, {KEY_RIGHTBRACE, "]"}, {KEY_BACKSLASH, "\\"},
        {KEY_SEMICOLON, ";"}, {KEY_APOSTROPHE, "'"}, {KEY_COMMA, ","},
        {KEY_DOT, "."}, {KEY_SLASH, "/"}, {KEY_102ND, "< >"},
        {KEY_SYSRQ, "Print Screen"}, {KEY_SCROLLLOCK, "Scroll Lock"}, {KEY_PAUSE, "Pause"},
        {KEY_INSERT, "Insert"}, {KEY_HOME, "Home"}, {KEY_PAGEUP, "Page Up"},
        {KEY_DELETE, "Delete"}, {KEY_END, "End"}, {KEY_PAGEDOWN, "Page Down"},
        {KEY_UP, "Up"}, {KEY_DOWN, "Down"}, {KEY_LEFT, "Left"}, {KEY_RIGHT, "Right"},
        {KEY_NUMLOCK, "Num Lock"}, {KEY_KPSLASH, "Keypad /"}, {KEY_KPASTERISK, "Keypad *"},
        {KEY_KPMINUS, "Keypad -"}, {KEY_KPPLUS, "Keypad +"}, {KEY_KPENTER, "Keypad Enter"},
        {KEY_KPDOT, "Keypad ."}, {KEY_KP0, "Keypad 0"}, {KEY_KP1, "Keypad 1"},
        {KEY_KP2, "Keypad 2"}, {KEY_KP3, "Keypad 3"}, {KEY_KP4, "Keypad 4"},
        {KEY_KP5, "Keypad 5"}, {KEY_KP6, "Keypad 6"}, {KEY_KP7, "Keypad 7"},
        {KEY_KP8, "Keypad 8"}, {KEY_KP9, "Keypad 9"},
        {KEY_PLAYPAUSE, "Play/Pause"}, {KEY_NEXTSONG, "Next Track"},
        {KEY_PREVIOUSSONG, "Previous Track"}, {KEY_STOPCD, "Stop"},
        {KEY_MUTE, "Mute"}, {KEY_VOLUMEUP, "Volume Up"}, {KEY_VOLUMEDOWN, "Volume Down"},
        {BTN_LEFT, "Left Click"}, {BTN_RIGHT, "Right Click"}, {BTN_MIDDLE, "Middle Click"},
        {BTN_SIDE, "Mouse Back (4)"}, {BTN_EXTRA, "Mouse Forward (5)"},
        {BTN_FORWARD, "Mouse Button 6"}, {BTN_BACK, "Mouse Button 7"}, {BTN_TASK, "Mouse Button 8"},
        {WheelUp, "Wheel Up"}, {WheelDown, "Wheel Down"},
    };
    return names;
}

QString label(int code)
{
    if (code == Hypershift)
        return QStringLiteral("Hypershift");
    const auto &pretty = prettyNames();
    if (auto it = pretty.constFind(code); it != pretty.constEnd())
        return it.value();

    QString n = name(code);
    if (n.startsWith(QLatin1String("KEY_")) || n.startsWith(QLatin1String("BTN_")))
        n = n.mid(4);
    if (n.size() > 1 && !(n.size() <= 3 && n.startsWith(QLatin1Char('F'))))
        n = n.left(1) + n.mid(1).toLower();
    return n;
}

QString comboLabel(const QList<int> &codes)
{
    if (codes.isEmpty())
        return QStringLiteral("Disabled");
    QStringList parts;
    for (int c : codes)
        parts << label(c);
    return parts.join(QStringLiteral(" + "));
}

QString bindingLabel(const Binding &binding, bool compact)
{
    if (binding.isCommand())
        return compact ? QStringLiteral("Run") : QStringLiteral("Run: %1").arg(binding.command);
    if (binding.isMacro()) {
        int keys = 0;
        for (const MacroStep &st : binding.macro)
            keys += st.kind == MacroStep::Press;
        const QString mode = binding.macroMode == Binding::RepeatWhileHeld ? QStringLiteral(", repeats while held")
                             : binding.macroMode == Binding::Toggle        ? QStringLiteral(", on/off")
                                                                           : QString();
        return compact ? QStringLiteral("Macro")
                       : QStringLiteral("Macro: %1 key %2%3")
                             .arg(keys)
                             .arg(keys == 1 ? QStringLiteral("press") : QStringLiteral("presses"), mode);
    }
    if (binding.isTapHold()) {
        const QString tap = binding.targets.isEmpty() ? QStringLiteral("nothing") : comboLabel(binding.targets);
        return compact ? QStringLiteral("%1 / %2").arg(tap, comboLabel(binding.hold))
                       : QStringLiteral("%1 · hold: %2").arg(tap, comboLabel(binding.hold));
    }
    QString text = comboLabel(binding.targets);
    if (binding.turbo > 0 && !binding.targets.isEmpty())
        text += (compact ? QStringLiteral(" ×%1/s") : QStringLiteral(" · turbo %1/s")).arg(binding.turbo);
    return text;
}

QString macroStepLabel(const MacroStep &step)
{
    switch (step.kind) {
    case MacroStep::Press: return QStringLiteral("Press %1").arg(label(step.value));
    case MacroStep::Release: return QStringLiteral("Release %1").arg(label(step.value));
    case MacroStep::Delay: return QStringLiteral("Wait %1 ms").arg(step.value);
    }
    return {};
}

QString macroSummary(const QList<MacroStep> &steps)
{
    int presses = 0;
    for (const MacroStep &st : steps)
        presses += st.kind == MacroStep::Press;
    return QStringLiteral("%1 %2, %3 key %4")
        .arg(steps.size())
        .arg(steps.size() == 1 ? QStringLiteral("step") : QStringLiteral("steps"))
        .arg(presses)
        .arg(presses == 1 ? QStringLiteral("press") : QStringLiteral("presses"));
}

bool isModifier(int code)
{
    switch (code) {
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
    case KEY_LEFTALT: case KEY_RIGHTALT:
    case KEY_LEFTMETA: case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

bool isValidTarget(int code)
{
    // Keep away from the joystick / gamepad / digitizer button ranges: if the
    // virtual device advertised those, udev and libinput would classify it as
    // a joystick or tablet instead of a keyboard + mouse.
    if (inRange(code, KEY_ESC, KEY_MICMUTE))
        return true;
    if (inRange(code, BTN_LEFT, BTN_TASK))
        return true;
    if (inRange(code, KEY_OK, BTN_TRIGGER_HAPPY - 1) && !inRange(code, BTN_DPAD_UP, BTN_DPAD_RIGHT))
        return libevdev_event_code_get_name(EV_KEY, code) != nullptr;
    return false;
}

bool isValidSource(int code)
{
    return code == WheelUp || code == WheelDown || inRange(code, 1, KEY_MAX);
}

bool isWheel(int code)
{
    return code == WheelUp || code == WheelDown;
}

QList<int> targetCodes()
{
    static const QList<int> codes = [] {
        QList<int> out;
        for (int c = 1; c < BTN_TRIGGER_HAPPY; ++c)
            if (isValidTarget(c) && libevdev_event_code_get_name(EV_KEY, c))
                out << c;
        return out;
    }();
    return codes;
}

QList<int> sourceCodes()
{
    QList<int> out = targetCodes();
    out << WheelUp << WheelDown;
    return out;
}

} // namespace Keys

namespace Keys {

unsigned modifierBit(int code)
{
    switch (code) {
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL: return ModCtrl;
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return ModShift;
    case KEY_LEFTALT: case KEY_RIGHTALT: return ModAlt;
    case KEY_LEFTMETA: case KEY_RIGHTMETA: return ModSuper;
    default: return 0;
    }
}

namespace {
const struct { unsigned bit; const char *name; } TriggerModifiers[] = {
    {ModCtrl, "Ctrl"}, {ModShift, "Shift"}, {ModAlt, "Alt"}, {ModSuper, "Super"},
};
} // namespace

QString triggerName(int t)
{
    QString out;
    for (const auto &m : TriggerModifiers)
        if (triggerModifiers(t) & m.bit)
            out += QLatin1String(m.name) + QLatin1Char('+');
    return out + name(triggerCode(t));
}

int triggerFromName(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('+'));
    unsigned mods = 0;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        bool known = false;
        for (const auto &m : TriggerModifiers)
            if (parts[i].compare(QLatin1String(m.name), Qt::CaseInsensitive) == 0) {
                mods |= m.bit;
                known = true;
            }
        if (!known)
            return -1;
    }
    const int code = fromName(parts.last());
    return code < 0 ? -1 : trigger(mods, code);
}

QString triggerLabel(int t)
{
    return shortcutLabel(triggerModifiers(t), triggerCode(t));
}

QString shortcutLabel(unsigned modifiers, int code)
{
    QStringList parts;
    if (modifiers & ModCtrl) parts << QStringLiteral("Ctrl");
    if (modifiers & ModShift) parts << QStringLiteral("Shift");
    if (modifiers & ModAlt) parts << QStringLiteral("Alt");
    if (modifiers & ModSuper) parts << QStringLiteral("Super");
    if (code > 0)
        parts << label(code);
    return parts.join(QStringLiteral(" + "));
}

} // namespace Keys
