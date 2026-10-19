#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/icons.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/controls/paging.h"
#include "interfaces/reader/ui/controls/panels.h"
#include "interfaces/reader/ui/style/theme.h"
#include "support/qt_font.h"
#include "support/strings.h"
#include "bookmark_list.h"
#include "interfaces/reader/ui/style/styles.h"
#include "interfaces/reader/ui/style/fonts.h"
#include <QLocale>
#include <QPainter>

namespace kobalt {
namespace c = ui::metrics::chrome;
namespace strip = ui::metrics::contents;
BookmarkList::BookmarkList(QWidget *parent) : Panel(parent) {
    setObjectName("rrBookmarkList");
    close = new ui::CellButton(ui::ControlIcon::Close, this);
    close->setObjectName("rrBookmarkListClose");
    close->setRule(Qt::RightEdge);
    close->setAccessibleName(strings::get("bookmarks.close"));
    connect(close, &QPushButton::clicked, this, [this] {
        if (back)
            back();
    });
    title = new QLabel(strings::get("bookmarks.title"), this);
    title->setObjectName("rrBookmarkListTitle");
    title->setTextFormat(Qt::PlainText);
    ui::readerStyle(title, ui::styles::brand(c::titleText) + "background:transparent;");
    position = new QLabel(this);
    position->setObjectName("rrBookmarkListPosition");
    position->setTextFormat(Qt::PlainText);
    position->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui::readerStyle(position, ui::styles::font(c::countText) + "background:transparent;");
    listHeader = new ui::ListHeader(this);
    listHeader->setTrailingInset(c::cellWidth);
    summary = listHeader->leftLabel();
    summary->setObjectName("rrBookmarkListSummary");
    pageHeading = listHeader->rightLabel();
    pageHeading->setObjectName("rrBookmarkListPageHeading");
    empty = new QLabel(strings::get("bookmarks.empty"), this);
    empty->setObjectName("rrBookmarkListEmpty");
    empty->setTextFormat(Qt::PlainText);
    empty->setAlignment(Qt::AlignCenter);
    ui::readerStyle(empty, ui::styles::font(c::labelText) + "background:transparent;");
    emptyHint = new QLabel(strings::get("bookmarks.empty_hint"), this);
    emptyHint->setObjectName("rrBookmarkListEmptyHint");
    emptyHint->setTextFormat(Qt::PlainText);
    emptyHint->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyHint->setWordWrap(true);
    ui::readerStyle(emptyHint,
                    ui::styles::font(c::descriptionText) + "color:palette(placeholder-text);background:transparent;");
    pager = new ui::PagerBar(this, "rrBookmarkList");
    pager->previous->setAccessibleName(strings::get("bookmarks.previous"));
    pager->next->setAccessibleName(strings::get("bookmarks.next"));
    connect(pager->previous, &QPushButton::clicked, this, [this] {
        if (pageCount() > 1) {
            page = pager->turnTarget(-1);
            redraw();
        }
    });
    connect(pager->next, &QPushButton::clicked, this, [this] {
        if (pageCount() > 1) {
            page = pager->turnTarget(1);
            redraw();
        }
    });
    hide();
}
int BookmarkList::pageCount() const {
    return qMax(1, (int(items.size()) + rows - 1) / rows);
}
void BookmarkList::showBookmarks(QVector<BookmarkRow> list, const QString &text) {
    // Keep the list page when the same list comes back after a removal.
    const bool reopening = !isVisible();
    items = std::move(list);
    position->setText(text);
    arrange();
    if (reopening) {
        page = 0;
        for (int i = 0; i < items.size(); ++i)
            if (items[i].current)
                page = i / rows;
    }
    redraw();
    show();
    raise();
}
void BookmarkList::resizeEvent(QResizeEvent *) {
    arrange();
    redraw();
}
void BookmarkList::paintEvent(QPaintEvent *event) {
    ui::Panel::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(0, c::actionHeight - c::heavyRule, width(), c::heavyRule, palette().color(QPalette::WindowText));
    painter.fillRect(0, c::actionHeight + strip::stripHeight - ui::metrics::control::border, width(),
                     ui::metrics::control::border, ui::dividerColor(this));
}
void BookmarkList::arrange() {
    const int first = page * rows, top = c::actionHeight + strip::stripHeight, available = height() - top;
    // Leave room for the pager only when the list needs more than one page.
    rows = qMax(1, available / ui::metrics::list::threeLines);
    if (items.size() > rows)
        rows = qMax(1, (available - c::actionHeight) / ui::metrics::list::threeLines);
    page = first / rows;
    const int barHeight = c::actionHeight - c::heavyRule, titleLeft = c::cellWidth + c::inset;
    close->setGeometry(0, 0, c::cellWidth, barHeight);
    title->setGeometry(titleLeft, 0, qMax(1, width() / 2 - titleLeft), barHeight);
    position->setGeometry(width() / 2, 0, qMax(1, width() / 2 - c::inset), barHeight);
    listHeader->setGeometry(0, c::actionHeight, width(), ui::metrics::list::headerHeight);
    pager->setGeometry(0, height() - c::actionHeight, width(), c::actionHeight);
    empty->setGeometry(c::inset, top + ui::metrics::list::threeLines / 2, qMax(1, width() - 2 * c::inset),
                       ui::metrics::list::threeLines / 2);
    emptyHint->setGeometry(2 * c::inset, top + ui::metrics::list::threeLines, qMax(1, width() - 4 * c::inset),
                           ui::metrics::list::threeLines);
}
void BookmarkList::redraw() {
    page = qBound(0, page, pageCount() - 1);
    empty->setVisible(items.isEmpty());
    emptyHint->setVisible(items.isEmpty());
    pageHeading->setVisible(!items.isEmpty());
    listHeader->setCaptions(strings::get("bookmarks.summary").arg(items.size()),
                            items.isEmpty() ? QString() : strings::get("chapter_contents.page"));
    pager->setVisible(items.size() > rows);
    pager->setPage(page, pageCount(), {}, true);
    while (buttons.size() < qMin(rows, int(items.size()))) {
        const int row = buttons.size();
        auto *button = new ui::ListRow(this);
        auto *remove = button->addAction({}, strings::get("bookmarks.remove"));
        // An unresolved bookmark still needs its independent removal action.
        remove->setParent(this);
        buttons.append(button);
        removes.append(remove);
        connect(button, &QPushButton::clicked, this, [this, row] {
            const int index = page * rows + row;
            if (index < items.size() && items[index].page >= 0 && selected)
                selected(items[index].position);
        });
        connect(remove, &QPushButton::clicked, this, [this, row] {
            const int index = page * rows + row;
            if (index < items.size() && removeRequested)
                removeRequested(items[index].position);
        });
    }
    int lastPage = 0;
    for (const auto &item : items)
        lastPage = qMax(lastPage, item.page);
    const int numberWidth = kobalt::qt::textWidth(QFontMetrics(ui::captionFont()), QString::number(lastPage + 1));
    for (int row = 0; row < buttons.size(); ++row) {
        const int index = page * rows + row;
        const bool shown = row < rows && index < items.size();
        buttons[row]->setVisible(shown);
        removes[row]->setVisible(shown);
        if (!shown)
            continue;
        const auto &item = items[index];
        const int y = c::actionHeight + strip::stripHeight + row * ui::metrics::list::threeLines;
        auto *button = buttons[row];
        button->setObjectName(QString("rrBookmark%1").arg(index));
        removes[row]->setObjectName(QString("rrBookmarkRemove%1").arg(index));
        button->setAccessibleName(item.chapter);
        button->setEnabled(item.page >= 0);
        button->setGeometry(0, y, width(), ui::metrics::list::threeLines);
        auto *itemRow = static_cast<ui::ListRow *>(button);
        itemRow->setTitle(item.chapter);
        itemRow->setDetail(item.excerpt);
        itemRow->setCaption(item.created.isValid() ? QLocale().toString(item.created.toLocalTime().date(), "d MMM yyyy")
                                                   : QString());
        itemRow->setAccessory(
            ui::ListRow::Caption{item.page >= 0 ? QString::number(item.page + 1) : QString(), numberWidth});
    }
}
}
