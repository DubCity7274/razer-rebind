#pragma once

#include "keys.h"
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
    void clearPressed();

signals:
    void keyClicked(int code);

protected:
    struct Region
    {
        int code = 0;     // 0 = drawn but not bindable (e.g. Fn)
        QString label;
        QPainterPath path;
        bool compact = false; // too small to also show the binding
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
    // Lay the regions out again (the generic view's change with its inputs).
    void relayout();
    QColor accent() const { return m_accent.isValid() ? m_accent : palette().color(QPalette::Highlight); }

    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
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
    QSet<int> m_pressed;
    int m_hover = -1;
};

class KeyboardView : public DeviceView
{
    Q_OBJECT
public:
    using DeviceView::DeviceView;

protected:
    QVector<Region> regions() const override;
    QSizeF designSize() const override { return {22.5, 7.0}; }
    void paintOverlay(QPainter &) const override;
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

    void setDevice(const RazerDevice &device);
    void noteInput(int code); // pressed on the device: give it a keycap

protected:
    QVector<Region> regions() const override;
    QSizeF designSize() const override;
    void paintOverlay(QPainter &) const override;

private:
    QList<int> codes() const;

    RazerDevice m_device;
    QHash<QString, QList<int>> m_seen; // by hidPath, in the order first pressed
};

// Which picture fits a device: only the models it was drawn from.
enum class DevicePicture { HuntsmanElite, Viper, Generic };
DevicePicture pictureFor(const RazerDevice &device);
