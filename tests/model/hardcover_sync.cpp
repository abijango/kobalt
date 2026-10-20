#include "platform/nickel/common/hardcover_progress.h"
#include "platform/nickel/common/hardcover_sync.h"
#include "storage/reader_storage.h"
#include "support/qt_compat.h"
#include "check.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <memory>

using namespace kobalt;
namespace {
const auto book = QStringLiteral("file:///mnt/onboard/books/Katabasis-R. F. Kuang.epub");
struct Harness {
    QTemporaryDir root;
    std::unique_ptr<HardcoverSync> sync;
    void build(const QString &config = {}) {
        require(root.isValid(), "Cannot create Hardcover sync directory");
        const auto preferences = root.filePath("reader-settings.json");
        const auto pending = root.filePath("hardcover-pending.json");
        const auto tool = root.filePath("NickelHardcover");
        require(QDir().mkpath(tool), "Cannot create NickelHardcover directory");
        require(QFile(tool + "/cli").open(QIODevice::WriteOnly), "Cannot create cli");
        if (!config.isEmpty()) {
            QFile file(tool + "/config.ini");
            require(file.open(QIODevice::WriteOnly) && file.write(config.toUtf8()) == config.size(),
                    "Cannot write NickelHardcover config");
        }
        require(storage::writeJson(preferences, qt::object({{"hardcover_progress_sync", true}})),
                "Cannot enable Hardcover sync");
        sync = std::make_unique<HardcoverSync>(HardcoverSync::Paths{tool, pending, preferences}, [] {
            return 1;
        });
    }
};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        Harness harness;
        harness.build();
        harness.sync->note(book, 40);
        require(harness.sync->pendingCount() == 0, "The NickelHardcover CLI can create a read and must not run");
    }
    {
        Harness harness;
        harness.build(QStringLiteral("auto_sync_default=true\nsync_on_close=1\nsync_on_read=20\n"));
        harness.sync->note(book, 40);
        require(harness.sync->pendingCount() == 0, "Kobalt ran the CLI while NickelHardcover auto-sync was enabled");
    }
    require(hardcoverToken(QStringLiteral("authorization=Bearer secret-value\n")) == QStringLiteral("secret-value"),
            "Bearer prefix stayed on the token");
    require(hardcoverToken(QStringLiteral("authorization=other\naccess_token=preferred\n")) ==
                QStringLiteral("preferred"),
            "access_token was ignored");
    require(hardcoverToken(QStringLiteral("[General]\n# comment\nauthorization=\"quoted\"\n")) ==
                QStringLiteral("quoted"),
            "Quoted authorization was not read");
    require(hardcoverToken(QStringLiteral("debug=true\n")).isEmpty(), "A config without a token looked configured");
    require(hardcoverBookPath(QStringLiteral("file:///mnt/onboard/book#part?%20.epub")) ==
                QStringLiteral("/mnt/onboard/book#part?%20.epub"),
            "Sideloaded ContentID was decoded as a URL");
    require(hardcoverBookPath(QStringLiteral("12345678-1234-1234-1234-123456789abc")) ==
                QStringLiteral("/mnt/onboard/.kobo/kepub/12345678-1234-1234-1234-123456789abc"),
            "Store ContentID was not read from the kepub directory");
    require(hardcoverBookPath(QStringLiteral("file://server/book.epub")).isEmpty() &&
                hardcoverBookPath(QStringLiteral("../book")).isEmpty() &&
                hardcoverBookPath(QStringLiteral("folder/book")).isEmpty(),
            "A non-local or escaped ContentID became an EPUB path");
    {
        const HardcoverRead active{6610852, 2661, 432, QStringLiteral("2026-10-12")};
        const auto update = hardcoverProgressUpdate({active}, 1, 1, 24);
        require(update.valid() && update.readId == active.id && update.editionId == active.editionId &&
                    update.progressPages == 104 && update.startedAt == active.startedAt,
                "One active read was not updated");
        require(!hardcoverProgressUpdate({}, 2661, 432, 20).valid(), "A missing active read could create a new read");
        require(!hardcoverProgressUpdate({active, active}, 2661, 432, 20).valid(),
                "Multiple active reads were guessed at");
    }
    {
        const auto activeRead = QJsonValue(qt::object({{"id", 90},
                                                       {"edition_id", 31889},
                                                       {"started_at", "2026-01-01"},
                                                       {"edition", qt::object({{"pages", 432}})}}));
        const auto books = qt::array({
            QJsonValue(qt::object(
                {{"id", 1}, {"status_id", 1}, {"edition_id", 10}, {"user_book_reads", qt::array({activeRead})}})),
            QJsonValue(qt::object({{"id", 2},
                                   {"status_id", 2},
                                   {"edition_id", 2661},
                                   {"edition", qt::object({{"pages", 400}})},
                                   {"user_book_reads", qt::array({activeRead})}})),
        });
        const auto chosen = hardcoverSelectUserBook(books);
        require(chosen.id == 2 && chosen.reads.size() == 1 && chosen.reads.first().id == 90,
                "Finished or non-current reads were considered active");
        const auto ambiguous = hardcoverSelectUserBook(qt::array({
            QJsonValue(qt::object(
                {{"id", 2}, {"status_id", 2}, {"edition_id", 2661}, {"user_book_reads", qt::array({activeRead})}})),
            QJsonValue(qt::object(
                {{"id", 3}, {"status_id", 2}, {"edition_id", 31889}, {"user_book_reads", qt::array({activeRead})}})),
        }));
        require(ambiguous.reads.size() == 2 && !hardcoverProgressUpdate(ambiguous.reads, 2661, 400, 33).valid(),
                "Multiple active user books selected an arbitrary read");
    }
    return 0;
}
