#include "theme.h"
#include "metrics.h"
#include <QPainter>
#include <QVariant>

namespace kobalt::ui {
namespace m = metrics;
void paintEdges(QPainter &painter, const QRect &rect, Edges edges, const QColor &ink, int width) {
    if (edges.testFlag(Qt::TopEdge))
        painter.fillRect(rect.x(), rect.y(), rect.width(), width, ink);
    if (edges.testFlag(Qt::BottomEdge))
        painter.fillRect(rect.x(), rect.bottom() - width + 1, rect.width(), width, ink);
    if (edges.testFlag(Qt::LeftEdge))
        painter.fillRect(rect.x(), rect.y(), width, rect.height(), ink);
    if (edges.testFlag(Qt::RightEdge))
        painter.fillRect(rect.right() - width + 1, rect.y(), width, rect.height(), ink);
}
void paintButtonShape(QPainter &painter, const QRect &rect, const QColor &paper, const QColor &fill,
                      const QColor &border) {
    painter.fillRect(rect, paper);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(border, m::control::border));
    painter.setBrush(fill);
    const qreal inset = int(m::control::border) / 2.0;
    painter.drawRoundedRect(QRectF(rect).adjusted(inset, inset, -inset, -inset), m::control::buttonRadius,
                            m::control::buttonRadius);
    painter.restore();
}
bool darkTheme(const QWidget *widget) {
    for (auto *ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
        const auto theme = ancestor->property("reader_dark");
        if (theme.isValid())
            return theme.toBool();
    }
    return false;
}
QColor dividerColor(const QWidget *widget) {
    return QColor(darkTheme(widget) ? m::chrome::dividerDark : m::chrome::dividerLight);
}
QColor noteColor(const QWidget *widget) {
    return QColor(darkTheme(widget) ? m::text::explanationDark : m::text::explanationLight);
}
void paintHeaderRules(QPainter &painter, const QWidget *view) {
    painter.fillRect(0, m::chrome::statusHeight - m::control::border, view->width(), m::control::border,
                     view->palette().color(QPalette::Mid));
    painter.fillRect(0, m::chrome::headerHeight - m::chrome::heavyRule, view->width(), m::chrome::heavyRule,
                     view->palette().color(QPalette::WindowText));
}
void readerStyle(QWidget *widget, const QString &source) {
    widget->setProperty("reader_style", source);
    const bool dark = darkTheme(widget);
    auto style = QString("QPushButton{border-radius:%1px;} ").arg(m::control::buttonRadius);
    style += source.contains('{') ? source : QString("*{%1}").arg(source);
    style += " QPushButton:disabled,QPushButton:checked:disabled,QPushButton:pressed:disabled{"
             "color:palette(mid);background:palette(window);border-color:palette(midlight);}";
    style.replace("palette(window-text)", dark ? "white" : "black");
    style.replace("palette(window)", dark ? "black" : "white");
    style.replace("palette(midlight)", dark ? "#333333" : "#dddddd");
    style.replace("palette(mid)", "#999999");
    style.replace("palette(light)", dark ? m::chrome::dividerDark : m::chrome::dividerLight);
    style.replace("palette(placeholder-text)", dark ? "#cccccc" : "#333333");
    style.replace("palette(dark)", dark ? m::control::darkOutline : m::control::lightOutline);
    widget->setStyleSheet(style);
}
}
