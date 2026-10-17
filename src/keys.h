#pragma once

#include <QHash>
#include <QList>
#include <QString>

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

    bool isHypershift() const;
    bool isTapHold() const { return !hold.isEmpty(); }
    // Whether holding this input switches to the Hypershift layer.
    bool triggersHypershift() const;
    int effectiveHoldTime() const;
    bool operator==(const Binding &o) const
    {
        return targets == o.targets && turbo == o.turbo && hold == o.hold && effectiveHoldTime() == o.effectiveHoldTime();
    }
};

// Source key code -> binding.
using BindingMap = QHash<int, Binding>;

namespace Keys {

// Pseudo codes for scroll wheel directions so they can be bound like keys.
// They sit above KEY_MAX and never reach the kernel.
constexpr int WheelUp = 0x1000;
constexpr int WheelDown = 0x1001;
// Pseudo target: hold for the Hypershift layer. Never emitted.
constexpr int Hypershift = 0x1100;

// Turbo rates offered in the editor, in presses per second.
constexpr int TurboMin = 1;
constexpr int TurboMax = 50;
constexpr int TurboDefault = 10;

// Tap vs hold: how long a press must last to count as a hold, in ms.
constexpr int HoldTimeMin = 100;
constexpr int HoldTimeMax = 1000;
constexpr int HoldTimeDefault = 200;

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
