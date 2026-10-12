#include "platform/nickel/common/hardcover_sync.h"
#include "storage/reader_storage.h"
#include "support/qt_compat.h"
#include "check.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <memory>

using namespace kobalt;
namespace {
const auto book = QStringLiteral("file:///mnt/onboard/books/Katabasis-R. F. Kuang.epub");
HardcoverSync::CommandResult result(int code) {
    HardcoverSync::CommandResult value;
    value.exitCode = code;
    value.reason = QStringLiteral("exited");
    return value;
}
QString nickelKey(QString contentId, const char *field) {
    contentId.replace(QLatin1Char('/'), QLatin1Char('-'));
    contentId.replace(QLatin1Char('\\'), QLatin1Char('-'));
    return contentId + QLatin1Char('/') + QLatin1String(field);
}
struct Harness {
    QTemporaryDir root;
    int link = 0;
    bool finishNow = false;
    int finishCode = 0;
    QVector<QStringList> attempts;
    QVector<std::function<void(const HardcoverSync::CommandResult &)>> waiting;
    std::unique_ptr<HardcoverSync> sync;
    QString preferences;
    QString pending;
    QString tool;
    void build() {
        require(root.isValid(), "Cannot create Hardcover sync directory");
        preferences = root.filePath("reader-settings.json");
        pending = root.filePath("hardcover-pending.json");
        tool = root.filePath("NickelHardcover");
        require(QDir().mkpath(tool), "Cannot create NickelHardcover directory");
        require(QFile(tool + "/cli").open(QIODevice::WriteOnly), "Cannot create cli");
        HardcoverSync::Paths paths{tool, pending, preferences};
        sync = std::make_unique<HardcoverSync>(
            paths,
            [this] {
                return link;
            },
            [this](QObject *, const QString &, const QStringList &arguments,
                   std::function<void(const HardcoverSync::CommandResult &)> finished) {
                attempts.append(arguments);
                if (finishNow)
                    finished(result(finishCode));
                else
                    waiting.append(std::move(finished));
            });
    }
    void finish(int code) {
        require(!waiting.isEmpty(), "No Hardcover command is waiting");
        auto done = waiting.takeFirst();
        done(result(code));
    }
    void enable() {
        require(storage::writeJson(preferences, qt::object({{"hardcover_progress_sync", true}})),
                "Cannot enable Hardcover sync");
    }
    QStringList take() {
        require(!attempts.isEmpty(), "Hardcover command was not started");
        return attempts.takeLast();
    }
};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        Harness harness;
        harness.build();
        harness.link = 1;
        harness.sync->note(book, 40);
        require(harness.attempts.isEmpty() && harness.sync->pendingCount() == 0, "Disabled sync started a command");
    }
    {
        Harness harness;
        harness.build();
        harness.enable();
        harness.sync->note(book, 40);
        harness.link = 1;
        harness.finishNow = true;
        harness.sync->note(QStringLiteral("file:///mnt/onboard/books/other.epub"), 1);
        require(harness.attempts.size() == 1 && harness.attempts.at(0).contains(QStringLiteral("40")),
                "A later short book did not flush the queued update");
        require(harness.sync->pendingCount() == 0, "Flushed update stayed queued");
    }
    {
        Harness harness;
        harness.build();
        harness.enable();
        harness.sync->note(book, 1);
        require(harness.attempts.isEmpty() && harness.sync->pendingCount() == 0, "Percent below 2 was queued");
        harness.sync->note(book, 40);
        require(harness.attempts.isEmpty() && harness.sync->pendingCount() == 1, "Offline update was sent or dropped");
        require(storage::readJson(harness.pending).value("pending").toArray().size() == 1,
                "Offline update was not stored");
        harness.link = 1;
        harness.sync->flush();
        const auto failed = harness.take();
        require(failed == QStringList({"update", "--value", "40", "--content-id", book}),
                "Hardcover command arguments changed");
        require(harness.sync->busy(), "Command returned before it finished");
        harness.finish(101);
        require(!harness.sync->busy() && harness.sync->pendingCount() == 1, "Network failure dropped the update");
        require(!QFile::exists(harness.tool + "/settings.ini"), "Failed command wrote NickelHardcover progress");
        harness.finishNow = true;
        harness.finishCode = 0;
        harness.sync->flush();
        require(!harness.sync->busy() && harness.sync->pendingCount() == 0,
                "Successful command left the update queued");
        QSettings settings(harness.tool + "/settings.ini", QSettings::IniFormat);
        require(settings.value(nickelKey(book, "progress")).toInt() == 40,
                "Successful command did not record progress");
        require(settings.value(nickelKey(book, "lastSynced")).toString().isEmpty(),
                "Successful command wrote lastSynced");
        require(!harness.attempts.last().contains(QStringLiteral("--after")),
                "A later update passed --after to a command that does not accept it");
        const int sent = harness.attempts.size();
        harness.sync->note(book, 40);
        require(harness.attempts.size() == sent && harness.sync->pendingCount() == 0,
                "The same percent was sent twice");
        harness.sync->note(book, 55);
        require(harness.attempts.size() == sent + 1 && harness.sync->pendingCount() == 0, "A new percent was not sent");
        settings.setValue(nickelKey(book, "enabled"), false);
        settings.sync();
        harness.sync->note(book, 70);
        require(harness.attempts.size() == sent + 1 && harness.sync->pendingCount() == 0,
                "A disabled NickelHardcover book was sent");
    }
    {
        Harness harness;
        harness.build();
        harness.enable();
        harness.link = -1;
        QSettings settings(harness.tool + "/settings.ini", QSettings::IniFormat);
        settings.setValue(nickelKey(book, "linkedbook"), "4242");
        settings.setValue(nickelKey(book, "lastSynced"), "2026-05-01T00:00:00Z");
        settings.setValue(nickelKey(book, "progress"), 15);
        settings.sync();
        harness.finishNow = true;
        harness.sync->note(book, 15);
        require(harness.attempts.isEmpty(), "NickelHardcover's stored percent was sent again");
        harness.sync->note(book, 22);
        const auto args = harness.take();
        require(args.contains(QStringLiteral("--linked-id")) && args.contains(QStringLiteral("4242")) &&
                    !args.contains(QStringLiteral("--after")) && !args.contains(QStringLiteral("--book-id")) &&
                    args.contains(QStringLiteral("22")),
                "Linked id was omitted or an unsupported flag was added");
    }
    {
        Harness harness;
        harness.build();
        harness.enable();
        harness.link = 1;
        require(QFile::remove(harness.tool + "/cli"), "Cannot hide the lowercase command");
        harness.sync->note(book, 30);
        require(harness.attempts.isEmpty() && harness.sync->pendingCount() == 1, "Missing command dropped the update");
        require(QFile(harness.tool + "/CLI").open(QIODevice::WriteOnly), "Cannot create CLI");
        harness.finishNow = true;
        harness.sync->flush();
        require(harness.attempts.size() == 1 && harness.sync->pendingCount() == 0, "Uppercase CLI was ignored");
        const auto other = QStringLiteral("file:///mnt/onboard/books/other.epub");
        harness.finishNow = false;
        harness.sync->note(book, 80);
        require(harness.sync->busy(), "First book did not hold the single command");
        harness.sync->note(other, 33);
        require(harness.attempts.size() == 2 && harness.sync->pendingCount() == 2,
                "Second book started early or was dropped");
        harness.finish(0);
        require(harness.attempts.size() == 3 && harness.sync->pendingCount() == 1 && harness.sync->busy(),
                "Queued book was not started after the first success");
        harness.finish(0);
        require(harness.sync->pendingCount() == 0, "Queued book was not sent");
        require(storage::readJson(harness.pending).value("sent").toObject().value(other).toInt() == 33,
                "Second book's percent was not remembered");
    }
    {
        QTemporaryDir root;
        require(root.isValid(), "Cannot create a restart directory");
        const auto preferences = root.filePath("reader-settings.json");
        const auto pending = root.filePath("hardcover-pending.json");
        const auto tool = root.filePath("NickelHardcover");
        require(QDir().mkpath(tool) && QFile(tool + "/cli").open(QIODevice::WriteOnly), "Cannot stage the command");
        require(storage::writeJson(preferences, qt::object({{"hardcover_progress_sync", true}})),
                "Cannot enable stored Hardcover sync");
        int link = 0;
        QVector<QStringList> attempts;
        auto runner = [&](QObject *, const QString &, const QStringList &arguments,
                          std::function<void(const HardcoverSync::CommandResult &)> finished) {
            attempts.append(arguments);
            if (link == 1)
                finished(result(0));
        };
        {
            HardcoverSync sync(
                {tool, pending, preferences},
                [&] {
                    return link;
                },
                runner);
            sync.note(book, 44);
            require(attempts.isEmpty() && sync.pendingCount() == 1, "Restart fixture sent while offline");
        }
        link = 1;
        HardcoverSync restored(
            {tool, pending, preferences},
            [&] {
                return link;
            },
            runner);
        require(attempts.size() == 1 && restored.pendingCount() == 0,
                "A stored update was not sent when the network was up");
    }
    return 0;
}
