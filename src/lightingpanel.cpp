#include "lightingpanel.h"

#include "config.h"
#include "ledmap.h"
#include "remapmanager.h"

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStandardItemModel>

LightingPanel::LightingPanel(Config *config, RemapManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_manager(manager)
{
    m_effect = new QComboBox;
    m_colours = new QComboBox;
    m_colours->addItem(tr("One colour"), LightingSettings::OneColour);
    m_colours->addItem(tr("Two colours"), LightingSettings::TwoColours);
    m_colours->addItem(tr("Random colours"), LightingSettings::RandomColours);
    m_colour1Button = new QPushButton;
    m_colour2Button = new QPushButton;
    for (QPushButton *b : {m_colour1Button, m_colour2Button}) {
        b->setAutoDefault(false);
        b->setToolTip(tr("Choose the colour"));
    }
    m_speed = new QComboBox;
    m_speed->addItem(tr("Fast"), 1);
    m_speed->addItem(tr("Medium"), 2);
    m_speed->addItem(tr("Slow"), 3);
    m_direction = new QComboBox;
    m_direction->addItem(tr("To the right"), 1);
    m_direction->addItem(tr("To the left"), 2);
    // Custom (per key)
    m_paint = new QPushButton(tr("Paint keys"));
    m_paint->setCheckable(true);
    m_paint->setToolTip(tr("Click keys on the picture to paint them with the brush colour; right-click to unlight "
                           "them. Turn off to edit bindings again."));
    // On is filled solid, like the layer switch, so it's clear clicks paint.
    m_paint->setStyleSheet(QStringLiteral(
        "QPushButton:checked { color: white; font-weight: bold; background: palette(highlight);"
        " border: 1px solid palette(highlight); border-radius: 4px; padding: 4px 10px; }"));
    m_brushButton = new QPushButton(tr("Brush"));
    m_unlitButton = new QPushButton(tr("Unlit keys"));
    m_edgeButton = new QPushButton(tr("Edges"));
    m_clearKeys = new QPushButton(tr("Clear keys"));
    for (QPushButton *b : {m_paint, m_brushButton, m_unlitButton, m_edgeButton, m_clearKeys})
        b->setAutoDefault(false);
    m_brightnessLabel = new QLabel(tr("Brightness:"));
    m_brightness = new QSlider(Qt::Horizontal);
    m_brightness->setRange(0, 100);
    m_brightness->setSingleStep(5);
    m_brightness->setPageStep(10);
    m_brightness->setFixedWidth(120);
    m_note = new QLabel;
    m_note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 4);
    row->addWidget(new QLabel(tr("Lighting:")));
    for (QWidget *w : std::initializer_list<QWidget *>{m_effect, m_colours, m_colour1Button, m_colour2Button, m_speed,
                                                        m_direction, m_paint, m_brushButton, m_unlitButton, m_edgeButton,
                                                        m_clearKeys})
        row->addWidget(w);
    row->addSpacing(16);
    row->addWidget(m_brightnessLabel);
    row->addWidget(m_brightness);
    row->addSpacing(16);
    row->addWidget(m_note, 1);

    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(300);
    connect(&m_commitTimer, &QTimer::timeout, this, &LightingPanel::commit);
    for (QComboBox *c : {m_effect, m_colours, m_speed, m_direction})
        connect(c, &QComboBox::activated, this, [this] {
            updateVisibility();
            commit();
        });
    connect(m_brightness, &QSlider::valueChanged, this, [this] { m_commitTimer.start(); });
    auto pick = [this](QPushButton *button, QColor *colour) {
        const QColor c = QColorDialog::getColor(*colour, this, tr("Lighting colour"));
        if (!c.isValid())
            return;
        *colour = c;
        setColour(button, c);
        commit();
    };
    connect(m_unlitButton, &QPushButton::clicked, this, [this, pick] { pick(m_unlitButton, &m_unlit); });
    connect(m_edgeButton, &QPushButton::clicked, this, [this, pick] { pick(m_edgeButton, &m_edge); });
    connect(m_brushButton, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_brush, this, tr("Brush colour"));
        if (c.isValid()) {
            m_brush = c;
            setColour(m_brushButton, c);
            m_paint->setChecked(true);
        }
    });
    connect(m_paint, &QPushButton::toggled, this, &LightingPanel::paintingChanged);
    connect(m_clearKeys, &QPushButton::clicked, this, [this] {
        m_keys.clear();
        commit();
    });
    connect(m_colour1Button, &QPushButton::clicked, this, [this, pick] { pick(m_colour1Button, &m_colour1); });
    connect(m_colour2Button, &QPushButton::clicked, this, [this, pick] { pick(m_colour2Button, &m_colour2); });
    hide();
}

void LightingPanel::setColour(QPushButton *button, const QColor &colour)
{
    QPixmap swatch(36, 16);
    swatch.fill(colour);
    QPainter p(&swatch);
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    p.end();
    button->setIcon(swatch);
    button->setIconSize(swatch.size());
    button->setToolTip(tr("%1 — click to change").arg(colour.name()));
}

void LightingPanel::setDevice(const RazerDevice *device)
{
    m_commitTimer.stop();
    const Lighting lighting = device ? Lighting(*device) : Lighting(RazerDevice());
    const QList<LightingSettings::Effect> effects = device ? lighting.effects() : QList<LightingSettings::Effect>();
    if (effects.isEmpty()) {
        m_paint->setChecked(false);
        hide();
        emit previewChanged({});
        return;
    }
    m_device = *device;
    m_effect->clear();
    for (LightingSettings::Effect e : effects)
        m_effect->addItem(LightingSettings::effectName(e), e);
    m_brightnessLabel->setVisible(lighting.hasBrightness());
    m_brightness->setVisible(lighting.hasBrightness());
    if (!lighting.read(&m_current))
        m_current = LightingSettings(); // without the daemon the current effect can't be read
    const LightingSettings saved = m_config->device(m_device.profileKey).lighting;
    display(saved.isValid() ? saved : m_current.isValid() ? m_current : LightingSettings{});
    m_note->setText(saved.isValid() ? tr("Saved with the “%1” profile").arg(m_config->activeProfile(m_device.profileKey))
                                    : tr("Changes are saved with the “%1” profile").arg(m_config->activeProfile(m_device.profileKey)));
    show();
}

void LightingPanel::display(const LightingSettings &s)
{
    const int effect = m_effect->findData(s.effect);
    m_effect->setCurrentIndex(effect >= 0 ? effect : m_effect->findData(LightingSettings::Spectrum));
    m_colours->setCurrentIndex(m_colours->findData(s.colours));
    m_speed->setCurrentIndex(m_speed->findData(s.speed));
    m_direction->setCurrentIndex(m_direction->findData(s.direction));
    {
        QSignalBlocker b(m_brightness);
        m_brightness->setValue(s.brightness);
    }
    m_colour1 = s.colour1;
    m_colour2 = s.colour2;
    m_keys = s.keys;
    m_unlit = s.unlit;
    m_edge = s.edge;
    setColour(m_colour1Button, m_colour1);
    setColour(m_colour2Button, m_colour2);
    setColour(m_brushButton, m_brush);
    setColour(m_unlitButton, m_unlit);
    setColour(m_edgeButton, m_edge);
    m_brushButton->setToolTip(tr("Brush: the colour keys are painted with"));
    m_unlitButton->setToolTip(tr("Keys you haven't painted"));
    m_edgeButton->setToolTip(tr("The light strips around the keyboard and the wrist rest"));
    updateVisibility();
    emitPreview();
}

void LightingPanel::updateVisibility()
{
    using L = LightingSettings;
    const auto e = L::Effect(m_effect->currentData().toInt());
    const bool multi = e == L::Breathing || e == L::Starlight || e == L::Ripple;
    // Ripple is one colour or random; the others offer two as well.
    const int two = m_colours->findData(L::TwoColours);
    if (auto *model = qobject_cast<QStandardItemModel *>(m_colours->model()))
        if (QStandardItem *item = model->item(two))
            item->setEnabled(e != L::Ripple);
    if (e == L::Ripple && m_colours->currentIndex() == two)
        m_colours->setCurrentIndex(m_colours->findData(L::OneColour));
    const auto colours = L::Colours(m_colours->currentData().toInt());
    const bool needsColour = e == L::Static || e == L::Reactive || (multi && colours != L::RandomColours);
    m_colours->setVisible(multi);
    m_colour1Button->setVisible(needsColour);
    m_colour2Button->setVisible(multi && colours == L::TwoColours);
    m_speed->setVisible(e == L::Reactive || e == L::Starlight);
    m_speed->setToolTip(e == L::Reactive ? tr("How long a pressed key stays lit") : tr("How fast the stars twinkle"));
    m_direction->setVisible(e == L::Wave);
    const bool custom = e == L::Custom;
    for (QWidget *w : std::initializer_list<QWidget *>{m_paint, m_brushButton, m_unlitButton, m_edgeButton, m_clearKeys})
        w->setVisible(custom);
    if (!custom && m_paint->isChecked())
        m_paint->setChecked(false);
    const bool lit = e != L::Off;
    m_brightness->setEnabled(lit);
}

LightingSettings LightingPanel::fromWidgets() const
{
    LightingSettings s;
    s.effect = LightingSettings::Effect(m_effect->currentData().toInt());
    s.colours = LightingSettings::Colours(m_colours->currentData().toInt());
    s.colour1 = m_colour1;
    s.colour2 = m_colour2;
    s.speed = m_speed->currentData().toInt();
    s.direction = m_direction->currentData().toInt();
    s.brightness = m_brightness->value();
    s.keys = m_keys;
    s.unlit = m_unlit;
    s.edge = m_edge;
    return s;
}

void LightingPanel::commit()
{
    m_commitTimer.stop();
    const QString key = m_device.profileKey;
    // The first change in any profile pins the current lighting to the
    // other profiles, so switching back to them undoes it.
    if (!m_config->device(key).lighting.isValid() && m_current.isValid())
        m_config->fillLighting(key, m_current);
    DeviceProfile p = m_config->device(key);
    p.lighting = fromWidgets();
    m_config->setDevice(key, p);
    m_config->save(nullptr);
    m_manager->applyProfile();
    m_note->setText(tr("Saved with the “%1” profile").arg(m_config->activeProfile(key)));
    emitPreview();
}

bool LightingPanel::isPainting() const
{
    return isVisible() && m_paint->isVisible() && m_paint->isChecked();
}

void LightingPanel::paintKey(int code, bool erase)
{
    const LedMap map = ledMap(m_device);
    if (map.edges.contains(code)) {
        if (erase)
            m_keys.remove(code);
        else
            m_keys.insert(code, m_brush);
    } else {
        const auto led = map.keys.constFind(code);
        if (led == map.keys.constEnd())
            return;
        // Keys sharing an LED (the dial's ring) change together.
        for (auto it = map.keys.cbegin(); it != map.keys.cend(); ++it)
            if (it.value() == *led) {
                if (erase)
                    m_keys.remove(it.key());
                else
                    m_keys.insert(it.key(), m_brush);
            }
    }
    // Show it at once; send it to the keyboard once a drag pauses.
    emitPreview();
    m_commitTimer.start();
}

void LightingPanel::emitPreview()
{
    // Only fixed colours can be shown; animated effects show nothing.
    const LedMap map = isVisible() || !m_device.hidPath.isEmpty() ? ledMap(m_device) : LedMap();
    const auto e = LightingSettings::Effect(m_effect->currentData().toInt());
    QHash<int, QColor> keys;
    if (e == LightingSettings::Custom) {
        for (auto it = map.keys.cbegin(); it != map.keys.cend(); ++it)
            keys.insert(it.key(), m_keys.value(it.key(), m_unlit));
        for (auto it = map.edges.cbegin(); it != map.edges.cend(); ++it)
            keys.insert(it.key(), m_keys.value(it.key(), m_edge));
    } else if (e == LightingSettings::Static) {
        for (auto it = map.keys.cbegin(); it != map.keys.cend(); ++it)
            keys.insert(it.key(), m_colour1);
        for (auto it = map.edges.cbegin(); it != map.edges.cend(); ++it)
            keys.insert(it.key(), m_colour1);
    }
    emit previewChanged(keys);
}
