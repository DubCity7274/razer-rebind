#pragma once

#include <QHash>
#include <QList>
#include <QString>

// One step of a macro.
struct MacroStep
{
    enum Kind { Press, Release, Delay };
    Kind kind = Press;
    int value = 0; // key code for Press / Release, milliseconds for Delay

    bool operator==(const MacroStep &o) const { return kind == o.kind && value == o.value; }
};

// What one key or button does instead of its normal job.
struct Binding
{
    // Output key codes, pressed in order and released in reverse. Empty means
    // "disabled": the input is swallowed. {Keys::Hypershift} makes the key
    // switch to the Hypershift layer while held.
    QList<int> targets;
    int turbo = 0; // presses per second while held; 0 = off
    // Tap vs hold: when set, `targets` is what a tap sends, and holding the
    // input longer than holdTime ms (or pressing and releasing another key
    // meanwhile) sends these instead, for as long as it's held.
    QList<int> hold;
    int holdTime = 0; // ms; 0 = Keys::HoldTimeDefault
    // A macro, played instead of `targets` when not empty.
    enum MacroMode { PlayOnce, RepeatWhileHeld, Toggle };
    QList<MacroStep> macro;
    MacroMode macroMode = PlayOnce;
    // A shell command run when the input is pressed, instead of any keys.
    QString command;
    // Mouse DPI: a sensitivity clutch (clutchDpi while held) or a step
    // through the DPI stages.
    enum DpiAction { NoDpi, DpiClutch, DpiStageUp, DpiStageDown, DpiStageCycle };
    DpiAction dpi = NoDpi;
    int clutchDpi = 400;

    bool isHypershift() const;
    bool isTapHold() const { return !hold.isEmpty(); }
    bool isMacro() const { return !macro.isEmpty(); }
    bool isCommand() const { return !command.isEmpty(); }
    bool isDpi() const { return dpi != NoDpi; }
    // "Do nothing": no keys and nothing else to do instead.
    bool isDisabled() const { return targets.isEmpty() && !isTapHold() && !isMacro() && !isCommand() && !isDpi(); }
    // Whether holding this input switches to the Hypershift layer.
    bool triggersHypershift() const;
    int effectiveHoldTime() const;
    bool operator==(const Binding &o) const
    {
        return targets == o.targets && turbo == o.turbo && hold == o.hold && effectiveHoldTime() == o.effectiveHoldTime()
               && macro == o.macro && (macro.isEmpty() || macroMode == o.macroMode) && command == o.command
               && dpi == o.dpi && (dpi != DpiClutch || clutchDpi == o.clutchDpi);
    }
};

inline Binding makeBinding(const QList<int> &targets, int turbo = 0, const QList<int> &hold = {}, int holdTime = 0)
{
    Binding b;
    b.targets = targets;
    b.turbo = turbo;
    b.hold = hold;
    b.holdTime = holdTime;
    return b;
}

// Trigger -> binding. A trigger is an input code, optionally with the
// modifiers that must be held for it (Keys::trigger()).
using BindingMap = QHash<int, Binding>;

namespace Keys {

// Pseudo codes for scroll wheel directions so they can be bound like keys.
// They sit above KEY_MAX and never reach the kernel.
constexpr int WheelUp = 0x1000;
constexpr int WheelDown = 0x1001;
// Pseudo target: hold for the Hypershift layer. Never emitted.
constexpr int Hypershift = 0x1100;

// Pseudo codes for the LEDs of a keyboard's edge light strips, by their place
// in the LED matrix; only used for lighting.
constexpr int EdgeLedBase = 0x2000;
constexpr int edgeLed(int row, int column) { return EdgeLedBase + row * 32 + column; }
constexpr bool isEdgeLed(int code) { return code >= EdgeLedBase && code < EdgeLedBase + 32 * 32; }

// Turbo rates offered in the editor, in presses per second.
constexpr int TurboMin = 1;
constexpr int TurboMax = 50;
constexpr int TurboDefault = 10;

// Tap vs hold: how long a press must last to count as a hold, in ms.
constexpr int HoldTimeMin = 100;
constexpr int HoldTimeMax = 1000;
constexpr int HoldTimeDefault = 200;

// Macros: at most this many steps, and pauses up to a minute.
constexpr int MacroMaxSteps = 500;
constexpr int MacroMaxDelay = 60000;
// A repeating macro without pauses still waits this long between rounds.
constexpr int MacroMinLoopGap = 10;

QString macroStepLabel(const MacroStep &step); // "Press A", "Wait 50 ms"
QString macroSummary(const QList<MacroStep> &steps); // "16 steps, 6 key presses"

// Triggers: an input code plus the modifiers (Keys::Modifier bits) that must
// be held, exactly, for a binding to apply. Plain codes have none.
constexpr int trigger(unsigned modifiers, int code) { return int(modifiers << 16) | code; }
constexpr int triggerCode(int trigger) { return trigger & 0xffff; }
constexpr unsigned triggerModifiers(int trigger) { return unsigned(trigger) >> 16; }
QString triggerName(int trigger);           // "Ctrl+Shift+KEY_S"
int triggerFromName(const QString &name);   // -1 when unknown
QString triggerLabel(int trigger);          // "Ctrl + Shift + S"

QString name(int code);              // "KEY_CAPSLOCK"
int fromName(const QString &name);   // -1 when unknown
QString label(int code);             // "Caps Lock"
QString comboLabel(const QList<int> &codes);
// "Ctrl + C · turbo 10/s", "Esc · hold: L Ctrl"; compact: "Ctrl + C ×10/s",
// "Esc / L Ctrl".
QString bindingLabel(const Binding &binding, bool compact = false);

bool isModifier(int code);

// Modifier bits for hotkeys; left and right keys count the same.
enum Modifier : unsigned { ModCtrl = 1, ModShift = 2, ModAlt = 4, ModSuper = 8 };
unsigned modifierBit(int code);   // 0 when not a modifier
QString shortcutLabel(unsigned modifiers, int code); // "Ctrl + Alt + 1"

bool isValidTarget(int code);        // can be emitted by the virtual device
bool isValidSource(int code);
bool isWheel(int code);
QList<int> targetCodes();
QList<int> sourceCodes();

} // namespace Keys

inline bool Binding::isHypershift() const
{
    return targets.size() == 1 && targets.first() == Keys::Hypershift;
}

inline bool Binding::triggersHypershift() const
{
    return isHypershift() || (hold.size() == 1 && hold.first() == Keys::Hypershift);
}

inline int Binding::effectiveHoldTime() const
{
    return holdTime > 0 ? holdTime : Keys::HoldTimeDefault;
}
