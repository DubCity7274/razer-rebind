#include "deviceview.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <linux/input-event-codes.h>

#include <algorithm>
#include <tuple>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QPainterPath roundedKey(qreal x, qreal y, qreal w, qreal h)
{
    constexpr qreal gap = 0.07;
    QPainterPath p;
    p.addRoundedRect(QRectF(x + gap, y + gap, w - 2 * gap, h - 2 * gap), 0.12, 0.12);
    return p;
}

} // namespace

DeviceView::DeviceView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DeviceView::setBindings(const BindingMap &bindings, const BindingMap &fallback)
{
    m_bindings = bindings;
    m_fallback = fallback;
    relayout();
}

void DeviceView::relayout()
{
    m_regions.clear();
    m_hover = -1;
    updateGeometry();
    update();
}

QList<int> DeviceView::boundCodes() const
{
    QList<int> out;
    for (const BindingMap *map : {&m_bindings, &m_fallback})
        for (auto it = map->cbegin(); it != map->cend(); ++it)
            if (Keys::triggerModifiers(it.key()) == 0 && !out.contains(it.key()))
                out << it.key();
    std::sort(out.begin(), out.end());
    return out;
}

const Binding *DeviceView::bindingFor(int code, bool *inherited) const
{
    if (inherited)
        *inherited = false;
    if (auto it = m_bindings.constFind(code); it != m_bindings.constEnd())
        return &*it;
    if (auto it = m_fallback.constFind(code); it != m_fallback.constEnd()) {
        if (inherited)
            *inherited = true;
        return &*it;
    }
    return nullptr;
}

void DeviceView::setAccent(const QColor &accent)
{
    m_accent = accent;
    update();
}

void DeviceView::setPressed(int code, bool pressed)
{
    if (pressed)
        m_pressed.insert(code);
    else
        m_pressed.remove(code);
    update();
}

void DeviceView::clearPressed()
{
    m_pressed.clear();
    update();
}

QSize DeviceView::minimumSizeHint() const
{
    const QSizeF d = designSize();
    return QSize(int(d.width() * 26), int(d.height() * 26));
}

QTransform DeviceView::transform() const
{
    const QSizeF d = designSize();
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    const qreal unit = std::min(area.width() / d.width(), area.height() / d.height());
    const QPointF origin(area.x() + (area.width() - d.width() * unit) / 2,
                         area.y() + (area.height() - d.height() * unit) / 2);
    QTransform t;
    t.translate(origin.x(), origin.y());
    t.scale(unit, unit);
    return t;
}

int DeviceView::regionAt(const QPointF &widgetPos) const
{
    if (m_regions.isEmpty())
        m_regions = regions();
    const QPointF p = transform().inverted().map(widgetPos);
    // Later regions are drawn on top, so they win.
    for (int i = int(m_regions.size()) - 1; i >= 0; --i)
        if (m_regions[i].code > 0 && m_regions[i].path.contains(p))
            return i;
    return -1;
}

void DeviceView::paintEvent(QPaintEvent *)
{
    if (m_regions.isEmpty())
        m_regions = regions();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QTransform t = transform();
    const qreal unit = t.m11();
    p.setTransform(t);

    paintDecoration(p);

    const QPalette pal = palette();
    const QColor base = pal.color(QPalette::Button);
    const QColor text = pal.color(QPalette::ButtonText);
    const QColor accent = this->accent();
    const QColor disabledTint(210, 70, 70);
    const QColor hypershiftTint(150, 90, 220);

    QFont labelFont = font();
    QFont bindFont = font();

    for (int i = 0; i < m_regions.size(); ++i) {
        const Region &r = m_regions[i];
        bool inherited = false;
        const Binding *binding = r.code > 0 ? bindingFor(r.code, &inherited) : nullptr;
        const bool bound = binding != nullptr;
        const bool disabled = bound && binding->targets.isEmpty();

        QColor fill = base;
        if (r.code == 0)
            fill = mix(base, pal.color(QPalette::Window), 0.6);
        else if (bound && binding->isHypershift())
            fill = mix(base, hypershiftTint, inherited ? 0.25 : 0.45);
        else if (bound && !inherited)
            fill = mix(base, disabled ? disabledTint : accent, 0.45);
        if (i == m_hover)
            fill = fill.lighter(118);
        if (m_pressed.contains(r.code))
            fill = accent;

        p.setPen(QPen(mix(base, text, 0.35), 1.0 / unit));
        p.setBrush(fill);
        p.drawPath(r.path);

        // Text is drawn in device pixels so it stays crisp at any scale.
        const QRectF box = t.mapRect(r.path.boundingRect()).adjusted(3, 2, -3, -2);
        p.save();
        p.resetTransform();
        p.setPen(m_pressed.contains(r.code) ? pal.color(QPalette::HighlightedText)
                                            : (r.code == 0 ? mix(text, base, 0.5) : text));
        const bool showBinding = bound && !r.compact && box.height() > unit * 0.7;
        labelFont.setPixelSize(std::max(8, int(unit * textScale() * (r.label.size() > 4 ? 0.22 : 0.28))));
        p.setFont(labelFont);
        p.drawText(box, (showBinding ? Qt::AlignTop : Qt::AlignVCenter) | Qt::AlignHCenter | Qt::TextWordWrap, r.label);
        if (showBinding) {
            bindFont.setPixelSize(std::max(7, int(unit * textScale() * 0.2)));
            bindFont.setBold(!inherited);
            p.setFont(bindFont);
            if (inherited && !m_pressed.contains(r.code))
                p.setPen(mix(text, base, 0.45));
            const QString out = QFontMetrics(bindFont).elidedText(
                QStringLiteral("→ ") + Keys::bindingLabel(*binding, true), Qt::ElideRight, int(box.width()));
            p.drawText(box, Qt::AlignBottom | Qt::AlignHCenter, out);
        }
        p.restore();
    }

    paintOverlay(p);
}

void DeviceView::mouseMoveEvent(QMouseEvent *e)
{
    const int hover = regionAt(e->position());
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void DeviceView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int i = regionAt(e->position());
    if (i >= 0)
        emit keyClicked(m_regions[i].code);
}

void DeviceView::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

bool DeviceView::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int i = regionAt(he->pos());
        if (i < 0) {
            QToolTip::hideText();
            return true;
        }
        const int code = m_regions[i].code;
        QString tip = QStringLiteral("<b>%1</b> <code>%2</code>").arg(Keys::label(code), Keys::name(code));
        bool inherited = false;
        if (const Binding *b = bindingFor(code, &inherited))
            tip += QStringLiteral("<br>→ ") + Keys::bindingLabel(*b).toHtmlEscaped()
                   + (inherited ? tr(" (from the Normal layer; click to override)") : QString());
        else
            tip += tr("<br>Click to rebind");
        QToolTip::showText(he->globalPos(), tip, this);
        return true;
    }
    if (e->type() == QEvent::Resize)
        m_regions.clear();
    return QWidget::event(e);
}

// --- Keyboard ---------------------------------------------------------------

QVector<DeviceView::Region> KeyboardView::regions() const
{
    QVector<Region> out;
    using K = std::tuple<int, const char *, qreal>;
    constexpr int Gap = -1;
    auto row = [&](qreal y, qreal x, std::initializer_list<K> keys, qreal h = 1) {
        for (const auto &[code, label, w] : keys) {
            if (code != Gap)
                out.push_back({code, QString::fromUtf8(label), roundedKey(x, y, w, h)});
            x += w;
        }
    };

    // Razer Huntsman Elite, US ANSI layout: main block, navigation cluster,
    // numpad, and the media keys above the numpad.
    row(0, 0, {{KEY_ESC, "Esc", 1}, {Gap, "", 1},
               {KEY_F1, "F1", 1}, {KEY_F2, "F2", 1}, {KEY_F3, "F3", 1}, {KEY_F4, "F4", 1}, {Gap, "", 0.5},
               {KEY_F5, "F5", 1}, {KEY_F6, "F6", 1}, {KEY_F7, "F7", 1}, {KEY_F8, "F8", 1}, {Gap, "", 0.5},
               {KEY_F9, "F9", 1}, {KEY_F10, "F10", 1}, {KEY_F11, "F11", 1}, {KEY_F12, "F12", 1}, {Gap, "", 0.25},
               {KEY_SYSRQ, "PrtSc", 1}, {KEY_SCROLLLOCK, "ScrLk", 1}, {KEY_PAUSE, "Pause", 1}, {Gap, "", 0.25},
               {KEY_PREVIOUSSONG, "⏮", 1}, {KEY_PLAYPAUSE, "⏯", 1}, {KEY_NEXTSONG, "⏭", 1}, {KEY_MUTE, "Mute", 1}});
    row(1.25, 0, {{KEY_GRAVE, "`", 1}, {KEY_1, "1", 1}, {KEY_2, "2", 1}, {KEY_3, "3", 1}, {KEY_4, "4", 1},
                  {KEY_5, "5", 1}, {KEY_6, "6", 1}, {KEY_7, "7", 1}, {KEY_8, "8", 1}, {KEY_9, "9", 1},
                  {KEY_0, "0", 1}, {KEY_MINUS, "-", 1}, {KEY_EQUAL, "=", 1}, {KEY_BACKSPACE, "Backspace", 2},
                  {Gap, "", 0.25}, {KEY_INSERT, "Ins", 1}, {KEY_HOME, "Home", 1}, {KEY_PAGEUP, "PgUp", 1},
                  {Gap, "", 0.25}, {KEY_NUMLOCK, "Num", 1}, {KEY_KPSLASH, "/", 1}, {KEY_KPASTERISK, "*", 1},
                  {KEY_KPMINUS, "-", 1}});
    row(2.25, 0, {{KEY_TAB, "Tab", 1.5}, {KEY_Q, "Q", 1}, {KEY_W, "W", 1}, {KEY_E, "E", 1}, {KEY_R, "R", 1},
                  {KEY_T, "T", 1}, {KEY_Y, "Y", 1}, {KEY_U, "U", 1}, {KEY_I, "I", 1}, {KEY_O, "O", 1},
                  {KEY_P, "P", 1}, {KEY_LEFTBRACE, "[", 1}, {KEY_RIGHTBRACE, "]", 1}, {KEY_BACKSLASH, "\\", 1.5},
                  {Gap, "", 0.25}, {KEY_DELETE, "Del", 1}, {KEY_END, "End", 1}, {KEY_PAGEDOWN, "PgDn", 1},
                  {Gap, "", 0.25}, {KEY_KP7, "7", 1}, {KEY_KP8, "8", 1}, {KEY_KP9, "9", 1}});
    row(2.25, 21.5, {{KEY_KPPLUS, "+", 1}}, 2);
    row(3.25, 0, {{KEY_CAPSLOCK, "Caps", 1.75}, {KEY_A, "A", 1}, {KEY_S, "S", 1}, {KEY_D, "D", 1},
                  {KEY_F, "F", 1}, {KEY_G, "G", 1}, {KEY_H, "H", 1}, {KEY_J, "J", 1}, {KEY_K, "K", 1},
                  {KEY_L, "L", 1}, {KEY_SEMICOLON, ";", 1}, {KEY_APOSTROPHE, "'", 1}, {KEY_ENTER, "Enter", 2.25},
                  {Gap, "", 3.5}, {KEY_KP4, "4", 1}, {KEY_KP5, "5", 1}, {KEY_KP6, "6", 1}});
    row(4.25, 0, {{KEY_LEFTSHIFT, "Shift", 2.25}, {KEY_Z, "Z", 1}, {KEY_X, "X", 1}, {KEY_C, "C", 1},
                  {KEY_V, "V", 1}, {KEY_B, "B", 1}, {KEY_N, "N", 1}, {KEY_M, "M", 1}, {KEY_COMMA, ",", 1},
                  {KEY_DOT, ".", 1}, {KEY_SLASH, "/", 1}, {KEY_RIGHTSHIFT, "Shift", 2.75},
                  {Gap, "", 1.25}, {KEY_UP, "↑", 1}, {Gap, "", 1.25},
                  {KEY_KP1, "1", 1}, {KEY_KP2, "2", 1}, {KEY_KP3, "3", 1}});
    row(4.25, 21.5, {{KEY_KPENTER, "Enter", 1}}, 2);
    // Fn is handled inside the razerkbd driver and never reaches userspace.
    row(5.25, 0, {{KEY_LEFTCTRL, "Ctrl", 1.25}, {KEY_LEFTMETA, "Super", 1.25}, {KEY_LEFTALT, "Alt", 1.25},
                  {KEY_SPACE, "Space", 6.25}, {KEY_RIGHTALT, "Alt", 1.25}, {0, "Fn", 1.25},
                  {KEY_COMPOSE, "Menu", 1.25}, {KEY_RIGHTCTRL, "Ctrl", 1.25},
                  {Gap, "", 0.25}, {KEY_LEFT, "←", 1}, {KEY_DOWN, "↓", 1}, {KEY_RIGHT, "→", 1},
                  {Gap, "", 0.25}, {KEY_KP0, "0", 2}, {KEY_KPDOT, ".", 1}});
    // The multi-function dial reports volume up/down when turned.
    row(6.25, 18.5, {{KEY_VOLUMEDOWN, "Dial ⟲", 2}, {KEY_VOLUMEUP, "Dial ⟳", 2}}, 0.75);
    for (Region &r : out)
        if (r.path.boundingRect().height() < 0.8)
            r.compact = true;
    return out;
}

void KeyboardView::paintOverlay(QPainter &p) const
{
    const QTransform t = p.transform();
    p.save();
    p.resetTransform();
    QFont f = font();
    f.setPixelSize(std::max(8, int(t.m11() * 0.22)));
    p.setFont(f);
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(t.mapRect(QRectF(0, 6.3, 18, 0.6)), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Click a key to rebind it. Keys light up while held."));
    p.restore();
}

// --- Mouse ------------------------------------------------------------------

namespace {

// Viper Ultimate seen from above, in design units (16 x 12.6).
QPainterPath mouseBody()
{
    QPainterPath body;
    body.addRoundedRect(QRectF(5, 0.3, 6, 12), 2.9, 3.4);
    return body;
}

} // namespace

QVector<DeviceView::Region> MouseView::regions() const
{
    const QPainterPath body = mouseBody();
    QPainterPath leftArea, rightArea;
    leftArea.addRect(QRectF(5, 0.3, 2.95, 4.6));
    rightArea.addRect(QRectF(8.05, 0.3, 2.95, 4.6));

    auto rect = [](qreal x, qreal y, qreal w, qreal h, qreal radius = 0.25) {
        QPainterPath p;
        p.addRoundedRect(QRectF(x, y, w, h), radius, radius);
        return p;
    };

    QVector<Region> out;
    out.push_back({BTN_LEFT, tr("Left"), body.intersected(leftArea)});
    out.push_back({BTN_RIGHT, tr("Right"), body.intersected(rightArea)});
    out.push_back({Keys::WheelUp, QStringLiteral("▲"), rect(7.5, 0.9, 1.0, 0.95, 0.3), true});
    out.push_back({BTN_MIDDLE, QStringLiteral("M"), rect(7.5, 1.95, 1.0, 1.1, 0.15), true});
    out.push_back({Keys::WheelDown, QStringLiteral("▼"), rect(7.5, 3.15, 1.0, 0.95, 0.3), true});
    // Ambidextrous side buttons. With the factory onboard mapping both sides
    // report the same codes, so each pair is rebound together.
    out.push_back({BTN_EXTRA, QStringLiteral("5"), rect(4.55, 4.9, 0.7, 1.3), true});
    out.push_back({BTN_SIDE, QStringLiteral("4"), rect(4.55, 6.35, 0.7, 1.3), true});
    out.push_back({BTN_EXTRA, QStringLiteral("5"), rect(10.75, 4.9, 0.7, 1.3), true});
    out.push_back({BTN_SIDE, QStringLiteral("4"), rect(10.75, 6.35, 0.7, 1.3), true});
    return out;
}

void MouseView::paintDecoration(QPainter &p) const
{
    const QPalette pal = palette();
    p.setPen(QPen(pal.color(QPalette::Mid), 1.5 / p.transform().m11()));
    p.setBrush(mix(pal.color(QPalette::Button), pal.color(QPalette::Window), 0.5));
    p.drawPath(mouseBody());
}

void MouseView::paintOverlay(QPainter &p) const
{
    // Callouts for the small regions that can't fit their binding inline.
    const QTransform t = p.transform();
    const struct { int code; QString name; QPointF anchor; QRectF box; Qt::Alignment align; } callouts[] = {
        {Keys::WheelUp, tr("Wheel up"), {8.5, 1.35}, {11.8, 0.2, 4.2, 1.2}, Qt::AlignLeft},
        {BTN_MIDDLE, tr("Wheel click"), {8.5, 2.5}, {11.8, 1.5, 4.2, 1.2}, Qt::AlignLeft},
        {Keys::WheelDown, tr("Wheel down"), {8.5, 3.6}, {11.8, 2.8, 4.2, 1.2}, Qt::AlignLeft},
        {BTN_EXTRA, tr("Forward (5)"), {4.55, 5.55}, {0, 4.7, 4.2, 1.2}, Qt::AlignRight},
        {BTN_SIDE, tr("Back (4)"), {4.55, 7.0}, {0, 6.4, 4.2, 1.2}, Qt::AlignRight},
    };

    p.save();
    p.resetTransform();
    const QPalette pal = palette();
    QFont nameFont = font();
    nameFont.setPixelSize(std::max(11, int(t.m11() * 0.42)));
    QFont bindFont = nameFont;
    bindFont.setBold(true);

    for (const auto &c : callouts) {
        const QRectF box = t.mapRect(c.box);
        const QPointF anchor = t.map(c.anchor);
        const QPointF edge(c.align == Qt::AlignLeft ? box.left() - 4 : box.right() + 4, box.center().y());
        p.setPen(QPen(pal.color(QPalette::Mid), 1, Qt::DotLine));
        p.drawLine(anchor, edge);

        bool inherited = false;
        const Binding *b = bindingFor(c.code, &inherited);
        p.setPen(pal.color(QPalette::WindowText));
        p.setFont(nameFont);
        p.drawText(box, c.align | Qt::AlignTop, c.name);
        p.setFont(bindFont);
        p.setPen(b && b->isHypershift() ? QColor(150, 90, 220)
                 : b && !inherited     ? accent()
                                       : pal.color(QPalette::PlaceholderText));
        const QString text = b ? QStringLiteral("→ ") + Keys::bindingLabel(*b) : tr("default");
        p.drawText(box, c.align | Qt::AlignBottom,
                   QFontMetrics(bindFont).elidedText(text, Qt::ElideRight, int(box.width())));
    }

    QFont noteFont = font();
    noteFont.setPixelSize(std::max(8, int(t.m11() * 0.28)));
    p.setFont(noteFont);
    p.setPen(pal.color(QPalette::PlaceholderText));
    p.drawText(t.mapRect(QRectF(0, 9.4, 4.6, 3.2)), Qt::AlignRight | Qt::AlignBottom | Qt::TextWordWrap,
               tr("The DPI button underneath is handled by the mouse firmware and can't be rebound. It "
                  "cycles through the DPI stages set below."));
    p.restore();
}

// --- generic view -------------------------------------------------------------

namespace {
constexpr int GenericColumns = 8;
constexpr qreal GenericKeyW = 2.0, GenericKeyH = 1.3, GenericTop = 1.4;
} // namespace

DevicePicture pictureFor(const RazerDevice &device)
{
    if (device.kind == RazerDevice::Keyboard && device.productId == 0x0226)
        return DevicePicture::HuntsmanElite;
    // Viper, Viper Ultimate (wired and wireless) and Viper 8KHz share the shape.
    if (device.kind == RazerDevice::Mouse
        && (device.productId == 0x0078 || device.productId == 0x007a || device.productId == 0x007b
            || device.productId == 0x0091))
        return DevicePicture::Viper;
    return DevicePicture::Generic;
}

void GenericView::setDevice(const RazerDevice &device)
{
    m_device = device;
    relayout();
}

void GenericView::noteInput(int code)
{
    QList<int> &seen = m_seen[m_device.hidPath];
    if (code > 0 && !seen.contains(code)) {
        seen << code;
        relayout();
    }
}

QList<int> GenericView::codes() const
{
    // Bound inputs first, in code order, then the rest as they were pressed.
    QList<int> out = boundCodes();
    for (int c : m_seen.value(m_device.hidPath))
        if (!out.contains(c))
            out << c;
    return out;
}

QSizeF GenericView::designSize() const
{
    const int rows = std::max(1, int((codes().size() + GenericColumns - 1) / GenericColumns));
    return {GenericColumns * GenericKeyW, GenericTop + rows * GenericKeyH + 0.2};
}

QVector<DeviceView::Region> GenericView::regions() const
{
    QVector<Region> out;
    const QList<int> list = codes();
    for (int i = 0; i < list.size(); ++i) {
        const qreal x = (i % GenericColumns) * GenericKeyW;
        const qreal y = GenericTop + (i / GenericColumns) * GenericKeyH;
        out.push_back({list[i], Keys::label(list[i]), roundedKey(x, y, GenericKeyW, GenericKeyH)});
    }
    return out;
}

void GenericView::paintOverlay(QPainter &p) const
{
    const QTransform t = p.transform();
    p.save();
    p.resetTransform();
    QFont f = font();
    f.setPixelSize(std::max(11, int(t.m11() * 0.3)));
    p.setFont(f);
    p.setPen(palette().color(QPalette::PlaceholderText));
    const QString what = m_device.kind == RazerDevice::Mouse ? tr("button") : tr("key");
    const QString text = codes().isEmpty()
                             ? tr("There's no picture of the %1 yet. Press a %2 on it and it shows up here; "
                                  "click it to rebind it.").arg(m_device.profileKey, what)
                             : tr("There's no picture of the %1 yet: press a %2 on it to add it here, then "
                                  "click it to rebind it.").arg(m_device.profileKey, what);
    p.drawText(t.mapRect(QRectF(0, 0, GenericColumns * GenericKeyW, GenericTop - 0.2)),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
    p.restore();
}
