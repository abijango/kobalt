#include "sliders.h"
#include "../style/metrics.h"
#include "../style/styles.h"
#include "../style/fonts.h"
#include "support/strings.h"
#include <QPainter>
#include <QResizeEvent>
#include "buttons.h"
#include "rows.h"
#include "ui/input.h"
#include <QStyle>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QtMath>
#include <algorithm>

namespace kobalt::ui {
namespace m = metrics;
SettingSlider::SettingSlider(QWidget *parent, int minimum, int maximum, int step) : QSlider(Qt::Horizontal, parent) {
    setRange(minimum, maximum);
    setSingleStep(step);
    setMinimumHeight(m::control::sliderHeight);
    // Nickel fixes QSlider at 80 pixels, which clips inside our scaled rows.
    readerStyle(this, QString("QSlider{min-height:%1px;max-height:%1px;}").arg(m::control::sliderHeight));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_AcceptTouchEvents);
}
void SettingSlider::setMarkers(const QVector<int> &values, MarkerMode mode) {
    QVector<int> next;
    for (const int value : values) {
        if (value < minimum() || value > maximum() || next.contains(value))
            continue;
        next.append(value);
    }
    std::sort(next.begin(), next.end());
    if (markers == next && markerMode == mode)
        return;
    markers = next;
    markerMode = mode;
    update();
}
void SettingSlider::nudge(int steps) {
    setValue(qBound(minimum(), value() + steps * singleStep(), maximum()));
    if (committed)
        committed(value());
}
void SettingSlider::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    const int x = positionForValue(value());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::WindowText), m::control::sliderMarkerWidth));
    for (const int marker : markers) {
        if (marker < minimum() || marker > maximum())
            continue;
        painter.drawLine(positionForValue(marker), m::control::sliderMarkerTop, positionForValue(marker),
                         m::control::sliderMarkerTop + m::control::sliderMarkerHeight);
    }
    painter.setPen(QPen(palette().color(QPalette::WindowText), m::control::border));
    painter.drawLine(m::control::sliderInset, height() / 2, width() - m::control::sliderInset, height() / 2);
    painter.fillRect(QRect(m::control::sliderInset, height() / 2 - m::control::sliderFillHeight / 2,
                           x - m::control::sliderInset, m::control::sliderFillHeight),
                     palette().color(QPalette::WindowText));
    painter.setBrush(palette().color(QPalette::Window));
    painter.setPen(QPen(palette().color(QPalette::WindowText), m::control::border));
    painter.drawEllipse(QPoint(x, height() / 2), m::control::sliderRadius, m::control::sliderRadius);
    painter.setPen(QPen(palette().color(QPalette::WindowText), m::control::border));
    painter.drawEllipse(QPoint(x, height() / 2), m::control::sliderInnerRadius, m::control::sliderInnerRadius);
}
bool SettingSlider::event(QEvent *event) {
    // The slider draws no hover state. See Button::event.
    if (isHoverEvent(event))
        return true;
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd) {
        const auto points = touchPositions(static_cast<QTouchEvent *>(event));
        if (event->type() == QEvent::TouchBegin) {
            tracking = points.size() == 1;
            original = value();
        }
        if (points.size() != 1)
            cancel();
        if (tracking && !points.isEmpty())
            moveTo(points.first().x());
        if (event->type() == QEvent::TouchEnd)
            finish();
        event->accept();
        return true;
    }
    if (event->type() == QEvent::TouchCancel) {
        cancel();
        event->accept();
        return true;
    }
    return QSlider::event(event);
}
void SettingSlider::mousePressEvent(QMouseEvent *event) {
    if (!isPointerMouse(event) || event->button() != Qt::LeftButton)
        return;
    tracking = true;
    original = value();
    moveTo(mousePosition(event).x());
}
void SettingSlider::mouseMoveEvent(QMouseEvent *event) {
    if (tracking)
        moveTo(mousePosition(event).x());
}
void SettingSlider::mouseReleaseEvent(QMouseEvent *event) {
    if (!isPointerMouse(event) || event->button() != Qt::LeftButton)
        return;
    if (tracking)
        moveTo(mousePosition(event).x());
    finish();
}
void SettingSlider::hideEvent(QHideEvent *event) {
    cancel();
    QSlider::hideEvent(event);
}
int SettingSlider::positionForValue(int value) const {
    const int span = qMax(1, width() - 2 * m::control::sliderInset);
    if (markerMode == MarkerMode::Stops && markers.size() > 1) {
        if (value <= markers.first()) {
            return m::control::sliderInset;
        }
        for (int index = 1; index < markers.size(); ++index) {
            if (value <= markers[index]) {
                const double fraction = double(value - markers[index - 1]) / (markers[index] - markers[index - 1]);
                return m::control::sliderInset + qRound(span * (index - 1 + fraction) / (markers.size() - 1));
            }
        }
        return m::control::sliderInset + span;
    }
    return m::control::sliderInset + QStyle::sliderPositionFromValue(minimum(), maximum(), value, span);
}
void SettingSlider::moveTo(qreal x) {
    if (markerMode == MarkerMode::Stops && markers.size() > 1) {
        const int span = qMax(1, width() - 2 * m::control::sliderInset);
        const int index =
            qBound(0, qRound((x - m::control::sliderInset) * (markers.size() - 1) / span), int(markers.size() - 1));
        setValue(markers[index]);
        return;
    }
    const int raw = QStyle::sliderValueFromPosition(minimum(), maximum(), qRound(x) - m::control::sliderInset,
                                                    qMax(1, width() - 2 * m::control::sliderInset));
    setValue(qBound(minimum(), minimum() + qRound(double(raw - minimum()) / singleStep()) * singleStep(), maximum()));
}
void SettingSlider::cancel() {
    if (tracking)
        setValue(original);
    tracking = false;
}
void SettingSlider::finish() {
    const bool changed = tracking && value() != original;
    tracking = false;
    if (changed && committed)
        committed(value());
}

SettingSlider *sliderRow(QWidget *parent, QVBoxLayout *rows, const char *name, const char *labelName,
                         const QString &label, const char *lessName, const char *moreName, int minimum, int maximum,
                         int step, std::function<void(int)> apply, const QString &prefix, const QString &suffix) {
    auto *row = new QWidget(parent);
    row->setMinimumHeight(m::control::stepSize);
    rows->addWidget(row);
    auto *line = horizontalRow();
    row->setLayout(line);
    parent = row;
    auto *caption = new QWidget(parent);
    caption->setFixedWidth(m::control::captionWidth);
    line->addWidget(caption);
    auto *captionRows = new QVBoxLayout(caption);
    captionRows->setContentsMargins(0, 0, m::control::captionRight, 0);
    captionRows->setSpacing(px(2));
    auto *title = new QLabel(label.section(':', 0, 0), caption);
    title->setObjectName(QString::fromLatin1(name) + "Caption");
    title->setWordWrap(true);
    readerStyle(title, styles::font(m::text::secondary));
    captionRows->addWidget(title);
    auto *current = new QHBoxLayout;
    current->setContentsMargins(0, 0, 0, 0);
    current->setSpacing(px(6));
    captionRows->addLayout(current);
    auto *prefixLabel = new QLabel(strings::get("controls.currently"), caption);
    auto *value = new QLabel(caption);
    value->setObjectName(labelName);
    const auto secondary = styles::font(m::control::captionValueText) + "color:palette(placeholder-text);";
    readerStyle(prefixLabel, secondary);
    readerStyle(value, secondary);
    current->addWidget(prefixLabel);
    current->addWidget(value);
    current->addStretch(1);
    auto *less = new StepButton(ControlIcon::Minus, parent);
    less->setObjectName(lessName);
    less->setAccessibleName(strings::get("controls.decrease").arg(label.section(':', 0, 0).toLower()));
    auto *slider = new SettingSlider(parent, minimum, maximum, step);
    slider->setObjectName(name);
    auto *more = new StepButton(ControlIcon::Plus, parent);
    more->setObjectName(moreName);
    more->setAccessibleName(strings::get("controls.increase").arg(label.section(':', 0, 0).toLower()));
    line->addWidget(less, 0, Qt::AlignVCenter);
    line->addWidget(slider, 1);
    line->addWidget(more, 0, Qt::AlignVCenter);
    slider->setAccessibleName(label.section(':', 0, 0));
    QObject::connect(slider, &QSlider::valueChanged, value, [value, prefix, suffix](int v) {
        value->setText(prefix + QString::number(v) + suffix);
    });
    QObject::connect(less, &QPushButton::clicked, slider, [slider] {
        slider->nudge(-1);
    });
    QObject::connect(more, &QPushButton::clicked, slider, [slider] {
        slider->nudge(1);
    });
    slider->committed = std::move(apply);
    return slider;
}
}
