#pragma once

#include "keys.h"

#include <QPainterPath>
#include <QSet>
#include <QWidget>

// Clickable picture of a device. Keys that have a binding are tinted and show
// their output; keys light up while physically held.
class DeviceView : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceView(QWidget *parent = nullptr);

    void setBindings(const BindingMap &bindings);
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

    const BindingMap &bindings() const { return m_bindings; }

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
