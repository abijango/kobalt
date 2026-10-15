#include "storage/bookmark_store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <utime.h>
#include <QTemporaryDir>
#include <cstdio>
#include "check.h"

using kobalt::Bookmark;
using kobalt::BookmarkStore;

namespace {
const QByteArray fingerprint = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
Bookmark mark(const QString &position, const QString &created) {
    return {position, "Chapter", "First words", QDateTime::fromString(created, Qt::ISODate).toUTC()};
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read bookmark fixture");
    return file.readAll();
}
}

int main() {
    // Keep legacy UUID keys, hash other server IDs, and fingerprint files.
    require(BookmarkStore::bookKey("3F2A1B4C-0000-4000-8000-00000000ABCD", {}) ==
                "kobo-3f2a1b4c-0000-4000-8000-00000000abcd",
            "A purchased book is not keyed by its Kobo ID");
    require(BookmarkStore::bookKey("file:///mnt/onboard/a.epub", fingerprint) ==
                "file-" + QString::fromLatin1(fingerprint),
            "A sideloaded book is not keyed by its fingerprint");
    require(BookmarkStore::bookKey("file:///mnt/onboard/a.epub", "short").isEmpty(),
            "An invalid fingerprint gave a key");
    require(BookmarkStore::bookKey("../../etc", fingerprint).isEmpty(), "An unknown identity gave a key");
    const auto numericKey = BookmarkStore::bookKey("12345", {});
    require(numericKey.startsWith("kobo-id-") && numericKey.size() == 72 &&
                numericKey == BookmarkStore::bookKey("12345", fingerprint),
            "A numeric server ID did not get a stable bookmark key");
    require(BookmarkStore::bookKey("Server-ID", {}) != BookmarkStore::bookKey("server-id", {}),
            "Distinct server IDs shared their bookmarks");
    for (const auto &identity : {QString(), QStringLiteral("."), QStringLiteral(".."), QStringLiteral("folder\\book"),
                                 QString("book") + QChar(0) + "other"}) {
        require(BookmarkStore::bookKey(identity, fingerprint).isEmpty(), "Unsafe server ID got an annotation key");
    }

    QTemporaryDir root;
    require(root.isValid(), "No temporary folder");
    {
        BookmarkStore numeric(root.path(), "12345", fingerprint, "Numeric server book");
        require(numeric.load(), "Bookmarks were unavailable for a numeric server ID");
        numeric.setBookmarks({mark("/body/DocFragment[1]/body/p[1].0", "2026-09-20T10:00:00Z")});
        require(numeric.save(), "Cannot save a numeric server book's bookmark");
        BookmarkStore reopened(root.path(), "12345", fingerprint, "Numeric server book");
        require(reopened.load() && reopened.bookmarks().size() == 1,
                "Numeric server bookmarks did not survive reopening");
    }
    {
        BookmarkStore store(root.path(), "file:///mnt/onboard/a.epub", fingerprint, "A");
        require(store.load() && store.bookmarks().isEmpty(), "A new book did not start empty");
        store.setBookmarks({mark("/body/DocFragment[1]/body/p[3].0", "2026-09-20T10:00:00Z"),
                            mark("/body/DocFragment[2]/body/p[1].0", "2026-09-21T10:00:00Z")});
        require(store.save(), "Saving failed");
        require(QFile::exists(store.folder() + "/bookmarks.json") && QFile::exists(store.folder() + "/book.json"),
                "Saving did not write both files");
    }
    {
        BookmarkStore store(root.path(), "file:///mnt/onboard/a.epub", fingerprint, "A");
        require(store.load() && store.bookmarks().size() == 2, "Saved bookmarks did not load");
        require(store.bookmarks()[1].created == QDateTime::fromString("2026-09-21T10:00:00Z", Qt::ISODate),
                "The creation date did not round-trip");
    }
    // An edited sideloaded file has a new fingerprint: its bookmarks follow the path.
    const QByteArray edited = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    {
        BookmarkStore store(root.path(), "file:///mnt/onboard/a.epub", edited, "A");
        require(store.load() && store.bookmarks().size() == 2 &&
                    store.adoptedFrom() == "file-" + QString::fromLatin1(fingerprint),
                "An edited file lost its bookmarks");
        require(store.save() && store.adoptedFrom().isEmpty() && QFile::exists(store.folder() + "/bookmarks.json"),
                "Adopted bookmarks were not saved under the new fingerprint");
    }
    // A third revision must use the most recently saved bookmarks, not the
    // first fingerprint in alphabetical order.
    {
        BookmarkStore latest(root.path(), "file:///mnt/onboard/a.epub", edited, "A");
        latest.setBookmarks({mark("latest", "2026-09-22T10:00:00Z")});
        require(latest.save(), "Could not update the second revision");
        QFile oldFile(root.path() + "/file-" + QString::fromLatin1(fingerprint) + "/bookmarks.json");
        QFile newFile(latest.folder() + "/bookmarks.json");
        const struct utimbuf oldTime {
            1000000000, 1000000000
        }, newTime{1000000010, 1000000010};
        require(utime(QFile::encodeName(oldFile.fileName()).constData(), &oldTime) == 0 &&
                    utime(QFile::encodeName(newFile.fileName()).constData(), &newTime) == 0,
                "Could not set revision times");
        BookmarkStore next(root.path(), "file:///mnt/onboard/a.epub", QByteArray(64, 'b'), "A");
        require(next.load() && next.bookmarks().size() == 1 && next.bookmarks().first().position == "latest",
                "A third revision adopted stale bookmarks");
    }
    // Reject a save beyond the read limit before replacing the previous list.
    {
        BookmarkStore store(root.path(), "3f2a1b4c-0000-4000-8000-00000000bbbb", {}, "Large");
        store.setBookmarks({mark("preserved", "2026-09-20T10:00:00Z")});
        require(store.save(), "Could not save the original bookmark");
        QVector<Bookmark> large;
        for (int i = 0; i < BookmarkStore::maxBookmarks; ++i) {
            auto bookmark = mark("p" + QString::number(i), "2026-09-20T10:00:00Z");
            bookmark.chapter = QString(200, QChar(0x4e00));
            bookmark.excerpt = QString(200, QChar(0x4e00));
            large.append(bookmark);
        }
        store.setBookmarks(large);
        QString error;
        require(!store.save(&error) && !error.isEmpty(), "An unreadable oversized bookmark file was saved");
        BookmarkStore reopened(root.path(), "3f2a1b4c-0000-4000-8000-00000000bbbb", {}, "Large");
        require(reopened.load() && reopened.bookmarks().size() == 1 &&
                    reopened.bookmarks().first().position == "preserved",
                "An oversized save replaced readable bookmarks");
    }
    // Another path never adopts them.
    {
        BookmarkStore store(root.path(), "file:///mnt/onboard/b.epub",
                            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "B");
        require(store.load() && store.bookmarks().isEmpty(), "Another book took these bookmarks");
    }
    // A failed load must not turn the old data into an editable empty list.
    for (const auto &original : {QByteArray("{\"bookmarks\": ["), QByteArray("{\"bookmarks\": null}"),
                                 QByteArray("{\"bookmarks\": \"wrong type\"}"), QByteArray("{\"bookmarks\": {}}"),
                                 QByteArray(BookmarkStore::maxFileBytes + 1, 'x')}) {
        QTemporaryDir damagedRoot;
        BookmarkStore store(damagedRoot.path(), "3f2a1b4c-0000-4000-8000-00000000abcd", {}, "C");
        require(QDir().mkpath(store.folder()), "Cannot create damaged bookmark folder");
        const auto path = store.folder() + "/bookmarks.json";
        QFile damaged(path);
        require(damaged.open(QIODevice::WriteOnly) && damaged.write(original) == original.size(),
                "Could not write a damaged file");
        damaged.close();
        QString error;
        require(!store.load(&error) && !error.isEmpty() && store.bookmarks().isEmpty(), "A damaged file loaded");
        require(error == (original.size() > BookmarkStore::maxFileBytes ? "too-large" : "invalid"),
                "Load error contains identifying data");
        store.setBookmarks({mark("new", "2026-09-20T10:00:00Z")});
        require(!store.save(&error) && error == "load-failed" && read(path) == original,
                "Editing after a failed load replaced the previous bookmark bytes");
    }
    {
        QTemporaryDir unreadableRoot;
        BookmarkStore store(unreadableRoot.path(), "3f2a1b4c-0000-4000-8000-00000000abcd", {}, "C");
        const auto path = store.folder() + "/bookmarks.json";
        require(QDir().mkpath(path), "Cannot create unreadable bookmark fixture");
        QString error;
        require(!store.load(&error) && error == "unreadable", "Unreadable bookmarks did not report a safe error");
        store.setBookmarks({mark("new", "2026-09-20T10:00:00Z")});
        require(!store.save(&error) && error == "load-failed" && QFileInfo(path).isDir(),
                "An unreadable bookmark file became writable after its failed load");
    }
    // A metadata failure must not commit a new list before reporting failure.
    {
        QTemporaryDir failedRoot;
        BookmarkStore store(failedRoot.path(), "3f2a1b4c-0000-4000-8000-00000000abcd", {}, "C");
        store.setBookmarks({mark("original", "2026-09-20T10:00:00Z")});
        require(store.save(), "Cannot create saved bookmark fixture");
        const auto path = store.folder() + "/bookmarks.json";
        const auto original = read(path);
        const auto metadata = store.folder() + "/book.json";
        require(QFile::remove(metadata) && QDir().mkdir(metadata), "Cannot obstruct bookmark metadata");
        store.setBookmarks({mark("replacement", "2026-09-20T10:00:00Z")});
        QString error;
        require(!store.save(&error) && error == "write-metadata" && read(path) == original,
                "A failed metadata save replaced the bookmark list or exposed its path");
    }
    // One bookmark per page: several on one page become one at the page start,
    // with the oldest date. Unresolved positions stay as they are.
    {
        BookmarkStore store(root.path(), "file:///mnt/onboard/a.epub", fingerprint, "A");
        store.setBookmarks({mark("p10", "2026-09-22T10:00:00Z"), mark("p11", "2026-09-20T10:00:00Z"),
                            mark("p40", "2026-09-23T10:00:00Z"), mark("gone", "2026-09-19T10:00:00Z")});
        const auto page = [](const QString &position) {
            if (position == "p10" || position == "p11")
                return 5;
            return position == "p40" ? 12 : -1;
        };
        const auto atPage = [](int page, const QDateTime &created) {
            return Bookmark{"start" + QString::number(page), "Chapter", "Merged", created};
        };
        require(store.merge(page, atPage), "Two bookmarks on one page did not merge");
        const auto &list = store.bookmarks();
        require(list.size() == 3 && list[0].position == "start5" &&
                    list[0].created == QDateTime::fromString("2026-09-20T10:00:00Z", Qt::ISODate),
                "The merged bookmark is not at the page start with the oldest date");
        require(list[1].position == "p40" && list[2].position == "gone", "A single or unresolved bookmark changed");
        require(!store.merge(page, atPage), "Merging again changed something");
    }
    // A page that opens a chapter starts with its heading; the excerpt leaves it out.
    {
        using kobalt::excerptAfterChapter;
        require(excerptAfterChapter("CHAPTER I. Down the Rabbit-Hole Alice was beginning",
                                    "CHAPTER I. Down the Rabbit-Hole") == "Alice was beginning",
                "The chapter title stayed in the excerpt");
        require(excerptAfterChapter("chapter i.  down the rabbit-hole Alice", "CHAPTER I. Down the Rabbit-Hole") ==
                    "Alice",
                "Case or spacing kept the chapter title in the excerpt");
        require(excerptAfterChapter("Chapter 10 Alice", "Chapter 1") == "Chapter 10 Alice",
                "A title that ends mid-word was removed");
        require(excerptAfterChapter("Alice was beginning", "CHAPTER I.") == "Alice was beginning",
                "An excerpt without the title changed");
        require(excerptAfterChapter("CHAPTER I.", "CHAPTER I.") == "CHAPTER I.", "A heading-only excerpt became empty");
        require(excerptAfterChapter("Alice", "") == "Alice", "An empty title changed the excerpt");
    }
    std::puts("PASS: bookmark keys, saving, loading, edited files, damaged files, one bookmark per page and excerpts");
}
