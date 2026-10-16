#include "options.h"
#include "../style/metrics.h"
#include "../style/styles.h"
#include "../style/fonts.h"
#include "support/strings.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPointer>

namespace kobalt::ui {
namespace m = metrics;
void DescribedOption::mouseReleaseEvent(QMouseEvent *event) {
    // Batch Qt's released-state paint with the changes made by clicked.
    const bool enabled = updatesEnabled();
    if (enabled)
        setUpdatesEnabled(false);
    QPointer<DescribedOption> alive(this);
    QPointer<QWidget> parent = parentWidget();
    const QRect oldGeometry = geometry();
    const bool visible = isVisible();
    QCheckBox::mouseReleaseEvent(event);
    if (alive && enabled)
        alive->setUpdatesEnabled(true);
    if (enabled && visible && parent && (!alive || !alive->isVisible()))
        parent->update(oldGeometry);
}
DescribedOption::Presentation::Presentation()
    : labelPixels(m::text::body), descriptionPixels(m::text::secondary),
      indicatorSize(m::settings::toggleWidth, m::settings::toggleHeight) {}
bool DescribedOption::Presentation::operator==(const Presentation &other) const {
    return indicator == other.indicator && dividers == other.dividers && horizontalInset == other.horizontalInset &&
           minimumRowHeight == other.minimumRowHeight && labelPixels == other.labelPixels &&
           descriptionPixels == other.descriptionPixels && indicatorSize == other.indicatorSize;
}
void DescribedOption::setPresentation(const Presentation &value) {
    if (appearance == value)
        return;
    appearance = value;
    setMinimumHeight(appearance.minimumRowHeight);
    updateGeometry();
    update();
}
DescribedOption::DescribedOption(const QString &text, const QString &description, QWidget *parent)
    : QCheckBox(text, parent), description(description) {
    setAccessibleDescription(description);
    auto policy = sizePolicy();
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumHeight(description.isEmpty() ? m::control::stepSize : m::settings::optionHeight);
    setFocusPolicy(Qt::NoFocus);
}
int DescribedOption::heightForWidth(int width) const {
    if (description.isEmpty())
        return m::control::stepSize;
    QFont descriptionFont = font();
    descriptionFont.setPixelSize(appearance.descriptionPixels);
    descriptionFont.setWeight(QFont::Normal);
    descriptionFont.setItalic(false);
    const auto bounds =
        QFontMetrics(descriptionFont)
            .boundingRect(QRect(0, 0,
                                qMax(1, width - 2 * appearance.horizontalInset - m::settings::labelLeft -
                                            appearance.indicatorSize.width() - m::settings::toggleGap),
                                0),
                          Qt::TextWordWrap, description);
    return qMax(appearance.minimumRowHeight,
                m::settings::descriptionTop + bounds.height() + m::settings::descriptionBottom);
}
void DescribedOption::resizeEvent(QResizeEvent *event) {
    QCheckBox::resizeEvent(event);
    setMinimumHeight(heightForWidth(event->size().width()));
}
bool DescribedOption::hitButton(const QPoint &position) const {
    return rect().contains(position);
}
void DescribedOption::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    const auto ink = palette().color(QPalette::WindowText);
    const auto paper = palette().color(QPalette::Window);
    QFont descriptionFont = font();
    descriptionFont.setPixelSize(appearance.descriptionPixels);
    descriptionFont.setWeight(QFont::Normal);
    descriptionFont.setItalic(false);
    const int textWidth = qMax(1, width() - 2 * appearance.horizontalInset - m::settings::labelLeft -
                                      appearance.indicatorSize.width() - m::settings::toggleGap);
    const int textBottom =
        description.isEmpty()
            ? m::settings::checkboxTop + m::settings::checkboxSize
            : m::settings::descriptionTop + QFontMetrics(descriptionFont)
                                                .boundingRect(QRect(0, 0, textWidth, 0), Qt::TextWordWrap, description)
                                                .height();
    const int textOffset = (height() - (textBottom - m::settings::checkboxTop)) / 2 - m::settings::checkboxTop;
    const int boxTop = (height() - appearance.indicatorSize.height()) / 2;
    const QRect box(width() - appearance.horizontalInset - appearance.indicatorSize.width(), boxTop,
                    appearance.indicatorSize.width(), appearance.indicatorSize.height());
    painter.fillRect(rect(), paper);
    if (appearance.dividers) {
        paintEdges(painter, rect(), appearance.dividers, dividerColor(this));
    }
    if (!isEnabled())
        painter.setOpacity(.45);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(box).adjusted(1, 1, -1, -1);
    if (appearance.indicator == Indicator::Radio) {
        painter.setPen(QPen(ink, px(2)));
        painter.setBrush(paper);
        painter.drawEllipse(track);
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            const qreal inset = track.width() / 4;
            painter.drawEllipse(track.adjusted(inset, inset, -inset, -inset));
        }
    } else if (appearance.indicator == Indicator::Square) {
        painter.setPen(QPen(ink, px(2)));
        painter.setBrush(isChecked() ? ink : paper);
        painter.drawRect(track);
        if (isChecked()) {
            painter.setPen(QPen(paper, px(4), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            QPolygonF check;
            // The tick is drawn for a 48 pixel box and scales with it.
            check << QPointF(box.left() + px(11), box.top() + px(24))
                  << QPointF(box.left() + px(21), box.top() + px(34))
                  << QPointF(box.left() + px(37), box.top() + px(14));
            painter.drawPolyline(check);
        }
    } else {
        painter.setPen(QPen(ink, m::control::border));
        painter.setBrush(isChecked() ? ink : paper);
        const qreal inset = int(m::control::border) / 2.0;
        const QRectF roundedTrack = QRectF(box).adjusted(inset, inset, -inset, -inset);
        painter.drawRoundedRect(roundedTrack, roundedTrack.height() / 2, roundedTrack.height() / 2);
        const int thumbInset = px(6), diameter = box.height() - 2 * thumbInset;
        const int thumbX = isChecked() ? box.right() - thumbInset - diameter + 1 : box.left() + thumbInset;
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? paper : ink);
        painter.drawEllipse(QRect(thumbX, box.top() + thumbInset, diameter, diameter));
    }
    painter.setPen(ink);
    QFont labelFont = font();
    labelFont.setPixelSize(appearance.labelPixels);
    labelFont.setWeight(QFont::Normal);
    labelFont.setItalic(false);
    painter.setFont(labelFont);
    painter.drawText(QRect(appearance.horizontalInset + m::settings::labelLeft, m::settings::checkboxTop + textOffset,
                           width() - 2 * appearance.horizontalInset - m::settings::labelLeft -
                               appearance.indicatorSize.width() - m::settings::toggleGap,
                           m::settings::checkboxSize),
                     Qt::AlignLeft | Qt::AlignVCenter, text());
    labelFont.setPixelSize(appearance.descriptionPixels);
    labelFont.setItalic(false);
    painter.setFont(labelFont);
    painter.setPen(darkTheme(this) ? QColor(m::text::explanationDark) : QColor(m::text::explanationLight));
    painter.drawText(QRect(appearance.horizontalInset + m::settings::labelLeft,
                           m::settings::descriptionTop + textOffset,
                           width() - 2 * appearance.horizontalInset - m::settings::labelLeft -
                               appearance.indicatorSize.width() - m::settings::toggleGap,
                           textBottom - m::settings::descriptionTop),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, description);
}
}
