#include "paging.h"
#include "../style/metrics.h"
#include "../style/styles.h"
#include "../style/fonts.h"
#include "../style/private_text.h"
#include "support/strings.h"
#include <QPainter>
#include <QResizeEvent>

namespace kobalt::ui {
namespace m = metrics;
PagerBar::PagerBar(QWidget *parent, const QString &prefix) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    counter = new QLabel(this);
    counter->setObjectName(prefix + "Page");
    counter->setAlignment(Qt::AlignCenter);
    readerStyle(counter, styles::font(m::chrome::countText) + "background:transparent;");
    previous = new CellButton(ControlIcon::Previous, this);
    previous->setObjectName(prefix + "Previous");
    next = new CellButton(ControlIcon::Next, this);
    next->setObjectName(prefix + "Next");
    previous->setAccessibleName(strings::get("common.previous_page"));
    next->setAccessibleName(strings::get("common.next_page"));
    previous->setRule(Qt::RightEdge);
    next->setRule(Qt::LeftEdge);
    setFixedHeight(m::chrome::actionHeight);
}
void PagerBar::resizeEvent(QResizeEvent *) {
    const int top = m::control::border, cell = m::chrome::cellWidth;
    previous->setGeometry(0, top, cell, height() - top);
    next->setGeometry(width() - cell, top, cell, height() - top);
    counter->setGeometry(cell, top, qMax(1, width() - 2 * cell), height() - top);
}
void PagerBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.fillRect(0, 0, width(), m::control::border, dividerColor(this));
}
void PagerBar::setPage(int page, int count, const QString &label, bool wrap) {
    count = qMax(1, count);
    page = qBound(0, page, count - 1);
    counter->setText(label.isEmpty() ? strings::get("navigation.page_count").arg(page + 1).arg(count) : label);
    this->page = page;
    this->count = count;
    this->wrap = wrap;
    previous->setEnabled(count > 1 && (wrap || page > 0));
    next->setEnabled(count > 1 && (wrap || page + 1 < count));
    setVisible(count > 1);
}
int PagerBar::turnTarget(int direction) const {
    if (count <= 1 || direction == 0) {
        return page;
    }
    const int target = page + (direction > 0 ? 1 : -1);
    if (wrap) {
        return target < 0 ? count - 1 : target >= count ? 0 : target;
    }
    return qBound(0, target, count - 1);
}
PageControls::PageControls(QWidget *parent, const QString &prefix, bool compact) : QWidget(parent), compact(compact) {
    // The enclosing panel owns the background and perimeter. A full-width
    // pager must not paint over its dropdown's left and right borders.
    setProperty("reader_paging", true);
    readerStyle(this, "QWidget[reader_paging=\"true\"]{background:transparent;}");
    counter = new QLabel(this);
    counter->setObjectName(prefix + "Page");
    counter->setAlignment(Qt::AlignCenter);
    previous = new Button(this);
    previous->setObjectName(prefix + "Previous");
    next = new Button(this);
    next->setObjectName(prefix + "Next");
    iconAction(previous, ControlIcon::Previous, strings::get("common.previous_page"));
    iconAction(next, ControlIcon::Next, strings::get("common.next_page"));
    for (auto *button : {previous, next})
        button->setFocusPolicy(Qt::NoFocus);
    setFixedHeight(m::navigation::height);
}
void PageControls::setCompact(bool value) {
    compact = value;
    arrange();
}
void PageControls::resizeEvent(QResizeEvent *) {
    arrange();
}
void PageControls::arrange() {
    const int margin = compact ? m::navigation::compactMargin : m::navigation::margin;
    const int buttonWidth = compact ? m::navigation::compactButtonWidth : m::navigation::buttonWidth;
    const int side = margin + buttonWidth + m::control::itemGap;
    previous->setGeometry(margin, 0, buttonWidth, height());
    next->setGeometry(width() - margin - buttonWidth, 0, buttonWidth, height());
    counter->setGeometry(side, 0, qMax(1, width() - 2 * side), height());
}
void PageControls::setPage(int page, int count) {
    count = qMax(1, count);
    page = qBound(0, page, count - 1);
    counter->setText(strings::get("common.page_fraction").arg(page + 1).arg(count));
    previous->setEnabled(page > 0);
    next->setEnabled(page + 1 < count);
}
PageNavigation::PageNavigation(QWidget *parent, const QString &prefix) {
    title = new PrivateTextLabel(parent);
    title->setObjectName(prefix + "Title");
    title->setAlignment(Qt::AlignCenter);
    title->setTextFormat(Qt::PlainText);
    readerStyle(title, styles::font(m::text::pageHeading, true));
    back = new BackButton(strings::get("common.back"), parent);
    back->setObjectName(prefix + "Back");
    back->setFocusPolicy(Qt::NoFocus);
    paging = new PageControls(parent, prefix);
    counter = paging->counter;
    previous = paging->previous;
    next = paging->next;
}
void PageNavigation::layout(const QSize &size, bool compact) {
    const int width = size.width(), height = size.height();
    title->setVisible(!compact);
    back->setVisible(!compact);
    title->setGeometry(m::navigation::headingSide, m::navigation::headingTop,
                       qMax(1, width - 2 * m::navigation::headingSide), m::navigation::height);
    back->setGeometry(m::navigation::backLeft, m::navigation::headingTop, m::navigation::backWidth,
                      m::navigation::height);
    const int y = height - (compact ? m::navigation::compactBottom : m::navigation::bottom);
    paging->setGeometry(0, y, width, m::navigation::height);
    paging->setCompact(compact);
}
void PageNavigation::setPage(int page, int count) {
    paging->setPage(page, count);
}
void PageNavigation::setPagingVisible(bool visible) {
    paging->setVisible(visible);
}
}
