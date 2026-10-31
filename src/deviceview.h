#pragma once

#include "keys.h"
#include "ledmap.h"
#include "razerdevice.h"

#include <QPainterPath>
#include <QSet>
#include <QWidget>

// Clickable picture of a device. Keys that have a binding are tinted and show
// their output; keys light up while physically held. When editing the
// Hypershift layer, `fallback` holds the normal bindings, shown dimmed on
// keys the layer leaves alone.
class DeviceView : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceView(QWidget *parent = nullptr);

    void setBindings(const BindingMap &bindings, const BindingMap &fallback = {});
    // Tint for bound keys; invalid = the palette's highlight colour.
    void setAccent(const QColor &accent);
    void setPressed(int code, bool pressed);
    // The lighting to show: each key's (and edge LED's) colour; empty shows
    // none.
    void setKeyColours(const QHash<int, QColor> &keys);
    // Painting lighting: clicks paint keys instead of editing bindings.
    void setPaintMode(bool paint);
    void clearPressed();
    // The device shown, and inputs pressed on it: views without a key for an
    // input give it a keycap, so everything the device sends can be bound.
    void setDevice(const RazerDevice &device);
    void noteInput(int code);

signals:
    void keyClicked(int code);
    void keyRightClicked(int code);

protected:
    struct Region
    {
        int code = 0;     // 0 = drawn but not clickable
        QString label;
        QPainterPath path;
        bool compact = false; // too small to also show the binding
        bool lightOnly = false; // only clickable while painting lighting (Fn, edge LEDs)
    };

    // Lay out regions in design units; the view scales them to fit.
    virtual QVector<Region> regions() const = 0;
    virtual QSizeF designSize() const = 0;
    virtual void paintDecoration(QPainter &) const {}
    virtual void paintOverlay(QPainter &) const {}
    virtual qreal textScale() const { return 1.0; }

    // The binding shown for a key: its own, else the fallback one (then
    // *inherited is set), else null.
    const Binding *bindingFor(int code, bool *inherited = nullptr) const;
    // Plain input codes with a binding here or in the fallback layer.
    QList<int> boundCodes() const;
    // Whether the lighting shown includes the edge strips' LEDs.
    bool showsEdgeLights() const;
    bool paintMode() const { return m_paintMode; }
    const RazerDevice &device() const { return m_device; }
    // Inputs pressed on the device since it was selected, in that order.
    QList<int> seenCodes() const { return m_seen.value(m_device.hidPath); }
    // Lay the regions out again (the generic view's change with its inputs).
    void relayout();
    QColor accent() const { return m_accent.isValid() ? m_accent : palette().color(QPalette::Highlight); }

    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    bool event(QEvent *) override;
    QSize minimumSizeHint() const override;

private:
    QTransform transform() const;
    int regionAt(const QPointF &widgetPos) const;

    mutable QVector<Region> m_regions;
    BindingMap m_bindings;
    BindingMap m_fallback;
    QColor m_accent;
    QHash<int, QColor> m_keyColours;
    bool m_paintMode = false;
    int m_dragRegion = -1;                   // painting: the region the drag is over
    Qt::MouseButton m_dragButton = Qt::NoButton;
    QSet<int> m_pressed;
    int m_hover = -1;
    RazerDevice m_device;
    QHash<QString, QList<int>> m_seen; // by hidPath, in the order first pressed
};

class KeyboardView : public DeviceView
{
    Q_OBJECT
public:
    using DeviceView::DeviceView;

protected:
    QVector<Region> regions() const override;
    QSizeF designSize() const override;
    void paintDecoration(QPainter &) const override;
    void paintOverlay(QPainter &) const override;

private:
    QVector<Region> mainRegions() const;                // the keys
    QVector<Region> withEdges(QVector<Region> out) const; // plus the Huntsman Elite's light strips
    KeyboardLayout layout() const;
    QSizeF keysSize() const; // the keys' area, without edge strips or extra keys
    // Inputs the picture has no key for, bound or pressed on the device.
    QList<int> extraCodes() const;
};

class MouseView : public DeviceView
{
    Q_OBJECT
public:
    using DeviceView::DeviceView;

protected:
    QVector<Region> regions() const override;
    QSizeF designSize() const override { return {16.0, 12.6}; }
    qreal textScale() const override { return 1.7; }
    void paintDecoration(QPainter &) const override;
    void paintOverlay(QPainter &) const override;
};

// For keyboards and mice without a picture of their own: a grid of keycaps
// for the inputs it knows about, the bound ones and everything pressed on
// the device since it was selected. Bindings work for every input either
// way; this only decides what can be clicked.
class GenericView : public DeviceView
{
    Q_OBJECT
public:
    using DeviceView::DeviceView;

protected:
    QVector<Region> regions() const override;
    QSizeF designSize() const override;
    void paintOverlay(QPainter &) const override;

private:
    QList<int> codes() const;
};

// Which picture fits a device: only the models it was drawn from.
// Keyboards with a known layout (see keyboardLayout()) use the keyboard
// picture, trimmed to their size.
enum class DevicePicture { Keyboard, Viper, Generic };
DevicePicture pictureFor(const RazerDevice &device);
