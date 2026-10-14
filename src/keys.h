#pragma once

#include <QHash>
#include <QList>
#include <QString>

// Source key code -> list of output key codes (pressed in order, released in
// reverse). An empty list means "disabled": the input is swallowed.
using BindingMap = QHash<int, QList<int>>;

namespace Keys {

// Pseudo codes for scroll wheel directions so they can be bound like keys.
// They sit above KEY_MAX and never reach the kernel.
constexpr int WheelUp = 0x1000;
constexpr int WheelDown = 0x1001;

QString name(int code);              // "KEY_CAPSLOCK"
int fromName(const QString &name);   // -1 when unknown
QString label(int code);             // "Caps Lock"
QString comboLabel(const QList<int> &codes);

bool isModifier(int code);

// Modifier bits for hotkeys; left and right keys count the same.
enum Modifier : unsigned { ModCtrl = 1, ModShift = 2, ModAlt = 4, ModSuper = 8 };
unsigned modifierBit(int code);   // 0 when not a modifier
QString shortcutLabel(unsigned modifiers, int code); // "Ctrl + Alt + 1"

bool isValidTarget(int code);        // can be emitted by the virtual device
bool isValidSource(int code);
QList<int> targetCodes();
QList<int> sourceCodes();

} // namespace Keys
