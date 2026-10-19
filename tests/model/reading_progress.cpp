#include "support/qt_compat.h"
#include "reading/reading_progress.h"
#include "platform/nickel/common/native_location.h"
#include "platform/nickel/common/local_book_path.h"
#include "platform/nickel/qt5/native_location.h"
#include <QCoreApplication>
#include "check.h"
using namespace kobalt;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    for (const auto &path : {QStringLiteral("/mnt/onboard/.sync/.books/.hidden.epub"),
                             QStringLiteral("/mnt/onboard/.kobo/dropbox/.nested/book.epub"),
                             QStringLiteral("/mnt/onboard/.kobo/google_drive/book.epub"),
                             QStringLiteral("/mnt/onboard/.adds/kobocloud/library/book.epub"),
                             QStringLiteral("/mnt/sd/kobocloud/book.epub"),
                             QStringLiteral("/mnt/third-party-sync/books/book#part?%20.epub"),
                             QStringLiteral("/mnt/third-party-sync/books/extensionless")}) {
        require(localReaderBookPath("file://" + path) == path,
                "Local EPUB path was restricted by its folder or changed by URL decoding");
    }
    for (const auto &identity :
         {QStringLiteral("https://example.org/book.epub"), QStringLiteral("file://server/book.epub"),
          QStringLiteral("file://relative.epub"), QStringLiteral("12345678-1234-1234-1234-123456789abc"), QString()}) {
        require(localReaderBookPath(identity).isEmpty(), "Nonlocal identity became a local EPUB path");
    }
    for (const auto &identity :
         {QStringLiteral("12345"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
          QStringLiteral("server-book_42"), QStringLiteral("book%2Fpart"), QString::fromUtf8("caf\xc3\xa9")}) {
        require(downloadedReaderBookPath(identity) == "/mnt/onboard/.kobo/kepub/" + identity,
                "Downloaded book ID was restricted to UUIDs or changed by URL decoding");
    }
    for (const auto &identity :
         {QString(), QStringLiteral("."), QStringLiteral(".."), QStringLiteral("../book"),
          QStringLiteral("folder/book"), QStringLiteral("folder\\book"), QStringLiteral("/absolute"),
          QStringLiteral("https://example.org/book"), QString("book") + QChar(0) + "other"}) {
        require(downloadedReaderBookPath(identity).isEmpty(), "Downloaded ID escaped its filename boundary");
    }
    for (const auto &name :
         {QStringLiteral("book.kepub.epub"), QStringLiteral("book#part.kepub.epub"),
          QStringLiteral("book?part.kepub.epub"), QStringLiteral("book%20part.kepub.epub"),
          QStringLiteral("book%23part.kepub.epub"), QString::fromUtf8("caf\xc3\xa9 book.kepub.epub")}) {
        for (const auto &root : {QStringLiteral("/mnt/onboard/"), QStringLiteral("/mnt/sd/")}) {
            const auto path = root + name;
            const auto identity = QStringLiteral("file://") + path;
            require(nickel4::localBookPath(identity) == path, "Sideloaded filename was decoded as a URL");
            for (const auto &prefix : {identity, path}) {
                require(nickel4::chapter(identity, prefix + "!OEBPS!chapter.xhtml#kobo.1.1") ==
                            "OEBPS/chapter.xhtml#kobo.1.1",
                        "Literal sideloaded filename prevented legacy chapter decoding");
            }
        }
    }
    require(nickel4::localBookPath("12345678-1234-1234-1234-123456789abc").isEmpty() &&
                nickel4::localBookPath("https://example.org/book.epub").isEmpty(),
            "Nonlocal book identity became a sideloaded path");
    require(publicationLocation("book", "book!OEBPS!chapter.xhtml#kobo.1.1:7") == "OEBPS/chapter.xhtml#kobo.1.1:7",
            "Legacy bookmark lost its chapter or offset");
    require(publicationLocation("book", "book!chapter.xhtml") == "chapter.xhtml",
            "Legacy chapter-only bookmark was not decoded");
    require(publicationLocation("book", "OEBPS/chapter!name.xhtml#id!part:3") == "OEBPS/chapter!name.xhtml#id!part:3",
            "Modern path or fragment was decoded as legacy");
    require(publicationLocation("book", "other!OEBPS!chapter.xhtml#id:0") == "other!OEBPS!chapter.xhtml#id:0",
            "Another book's bookmark was accepted");
    require(publicationLocation("", "!chapter.xhtml") == "!chapter.xhtml",
            "Missing book identity decoded a legacy bookmark");
    ReadingProgress first{"synthetic", "chapter.xhtml#p:4", 1000, 1, true};
    auto second = first;
    second.location = "chapter.xhtml#p:12";
    second.modified = 2000;
    require(!preferNativeProgress(first, first), "Matching native baseline discarded local position");
    require(preferNativeProgress(second, first), "Newer native position lost to local save");
    require(!preferNativeProgress(first, second), "Older native position replaced a newer checkpoint");
    require(!preferNativeProgress({}, second), "Native read failure discarded local checkpoint");
    require(preferNativeProgress(first, {}), "Initial native position was not imported");
    auto duplicate = first;
    ++duplicate.modified;
    require(!preferNativeProgress(duplicate, first), "Unchanged position timestamp discarded local progress");
    auto finished = first;
    ++finished.modified;
    finished.status = 2;
    require(preferNativeProgress(finished, first), "Newer finished status was ignored");
    second.modified = first.modified;
    require(!preferNativeProgress(second, first), "Ambiguous equal-date position replaced local checkpoint");
    require(ReadingProgress::fromJson(first.json()) == first, "Baseline did not survive local persistence");
    require(!ReadingProgress{}.samePosition({}), "Failed native reads compared equal");
    ProgressReconciler choices(first);
    second.modified = 2000;
    choices.observe(second);
    require(choices.pending() && !choices.mayWrite(first), "Unanswered update allowed writeback");
    choices.observe(second);
    require(choices.candidate() == second, "Duplicate replaced pending candidate");
    auto unrelated = second;
    unrelated.book = "another book";
    unrelated.modified = 4000;
    choices.observe(unrelated);
    choices.observe(first);
    choices.observe({});
    require(choices.candidate() == second, "Unrelated, stale or invalid update replaced the candidate");
    auto third = second;
    third.location = "chapter.xhtml#p:18";
    third.modified = 3000;
    choices.observe(third);
    choices.acknowledge(second);
    require(choices.pending() && !choices.mayWrite(third), "Old alert accepted an unseen newer update");
    choices.acknowledge(third);
    require(!choices.pending() && choices.mayWrite(third), "Explicit choice did not allow writeback");
    choices.observe(third);
    require(!choices.pending(), "Own-save echo or duplicate reopened the alert");
    auto dated = third;
    dated.modified = 4000;
    choices.observe(dated);
    require(!choices.pending() && choices.mayWrite(dated), "Timestamp-only change reopened the alert");
    finished = dated;
    finished.modified = 5000;
    finished.status = 2;
    choices.observe(finished);
    require(choices.pending(), "Changed finished status did not ask for a choice");
    choices.acknowledge(finished);
    require(choices.mayWrite(finished), "Finished choice remained unresolved");
    choices.observe(third);
    require(!choices.pending() && !choices.mayWrite(third), "Stale persisted state was authorized for overwrite");
    ProgressReconciler failedRead({"synthetic", {}, 0, 0, false});
    failedRead.observe(first);
    require(failedRead.pending(), "Recovered native position was silently accepted");
    qInfo("Reading progress reconciliation checks passed");
}
