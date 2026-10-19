#include "hardcover_sync.h"

#include "storage/reader_storage.h"
#include "support/diagnostics.h"
#include "support/qt_compat.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <memory>

#include <dlfcn.h>

namespace kobalt {
namespace {
QString redactedOutput(const QByteArray &bytes) {
    auto output = QString::fromUtf8(bytes).simplified().left(180);
    if (output.contains(QStringLiteral("Bearer"), Qt::CaseInsensitive) ||
        output.contains(QStringLiteral("authorization"), Qt::CaseInsensitive))
        return QStringLiteral("redacted");
    return output;
}
void finishCommand(QProcess *process, const std::shared_ptr<bool> &completed, const QString &reason, int code,
                   const std::function<void(const HardcoverSync::CommandResult &)> &finished) {
    if (*completed)
        return;
    *completed = true;
    HardcoverSync::CommandResult result;
    result.exitCode = code;
    result.reason = reason;
    result.output = redactedOutput(process->readAllStandardOutput());
    result.errorOutput = redactedOutput(process->readAllStandardError());
    if (result.errorOutput.isEmpty() && reason != QStringLiteral("exited"))
        result.errorOutput = redactedOutput(process->errorString().toUtf8());
    process->deleteLater();
    finished(result);
}
void runDeviceCommand(QObject *parent, const QString &program, const QStringList &arguments,
                      std::function<void(const HardcoverSync::CommandResult &)> finished) {
    auto *process = new QProcess(parent);
    process->setWorkingDirectory(QFileInfo(program).absolutePath());
    auto completed = std::make_shared<bool>(false);
    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                     [process, finished, completed](int code, QProcess::ExitStatus status) {
                         finishCommand(process, completed,
                                       status == QProcess::NormalExit ? QStringLiteral("exited")
                                                                      : QStringLiteral("crashed"),
                                       status == QProcess::NormalExit ? code : -1, finished);
                     });
    process->start(program, arguments);
    // Only a launch failure is visible on the next turn. A fast exit is reported
    // by finished(), after stdout and stderr have been collected.
    qt::later(0, process, [process, finished, completed] {
        if (process->state() == QProcess::NotRunning && process->error() == QProcess::FailedToStart)
            finishCommand(process, completed, QStringLiteral("failed-to-start"), -1, finished);
    });
}
HardcoverSync::CliFlags probeCli(const QString &program) {
    HardcoverSync::CliFlags flags;
    flags.linkedFlag = QStringLiteral("--linked-id");
    QProcess process;
    process.setWorkingDirectory(QFileInfo(program).absolutePath());
    process.start(program, {QStringLiteral("update"), QStringLiteral("--help")});
    if (!process.waitForFinished(2000))
        return flags;
    const auto help = QString::fromUtf8(process.readAllStandardOutput() + '\n' + process.readAllStandardError());
    if (help.contains(QStringLiteral("--book-id")) && !help.contains(QStringLiteral("--linked-id")))
        flags.linkedFlag = QStringLiteral("--book-id");
    flags.after = help.contains(QStringLiteral("--after"));
    return flags;
}
int deviceNetwork() {
    using Manager = QObject *(*)();
    using Accessible = bool (*)(QObject *);
    const auto manager =
        reinterpret_cast<Manager>(dlsym(RTLD_DEFAULT, "_ZN23WirelessWorkflowManager14sharedInstanceEv"));
    const auto accessible =
        reinterpret_cast<Accessible>(dlsym(RTLD_DEFAULT, "_ZN23WirelessWorkflowManager20isInternetAccessibleEv"));
    if (!manager || !accessible)
        return -1;
    auto *instance = manager();
    if (!instance)
        return -1;
    return accessible(instance) ? 1 : 0;
}
HardcoverSync *deviceInstance = nullptr;
}
HardcoverSync::HardcoverSync(Paths paths, std::function<int()> network, Runner runner, Probe probe, QObject *parent)
    : QObject(parent), paths(std::move(paths)), network(std::move(network)), runner(std::move(runner)),
      probe(probe ? std::move(probe) : Probe(probeCli)),
      progressClient(new HardcoverProgress(paths.toolDirectory + QStringLiteral("/config.ini"), this)) {
    load();
    watchNetwork();
    flush();
}
void HardcoverSync::install(const QString &preferencesPath) {
    if (deviceInstance || !qApp)
        return;
    Paths paths;
    paths.toolDirectory = QStringLiteral("/mnt/onboard/.adds/NickelHardcover");
    paths.preferencesPath = preferencesPath;
    paths.pendingPath = QFileInfo(preferencesPath).dir().filePath(QStringLiteral("hardcover-pending.json"));
    deviceInstance = new HardcoverSync(std::move(paths), deviceNetwork, runDeviceCommand, probeCli, qApp);
}
HardcoverSync *HardcoverSync::installed() {
    return deviceInstance;
}
void HardcoverSync::note(const QString &contentId, int percent) {
    if (!featureEnabled()) {
        diagnostic("hardcover-skipped", {{"reason", "disabled"}, {"percent", percent}}, false);
        return;
    }
    // An empty id or a negative percent only retries updates already queued.
    if (contentId.isEmpty() || percent < 0) {
        flush();
        return;
    }
    const char *skip = nullptr;
    if (percent < hardcoverMinimumPercent || percent > 100)
        skip = "below-minimum";
    else if (!bookAllowed(contentId))
        skip = "book-disabled";
    else if (recordedPercent(contentId) == percent)
        skip = "unchanged";
    if (skip) {
        diagnostic("hardcover-skipped", {{"reason", skip}, {"percent", percent}}, false);
        if (removePending(contentId))
            persist();
        flush();
        return;
    }
    bool changed = true;
    bool found = false;
    for (auto &item : pending) {
        if (item.contentId != contentId)
            continue;
        found = true;
        changed = item.percent != percent;
        item.percent = percent;
        break;
    }
    if (!found)
        pending.append({contentId, percent});
    if (changed)
        diagnostic("hardcover-queued", {{"percent", percent}}, false);
    persist();
    flush();
}
void HardcoverSync::flush() {
    watchNetwork();
    if (running || !featureEnabled() || pending.isEmpty() || !runner)
        return;
    const bool graphql = progressClient && progressClient->configured();
    const auto cli = graphql ? QString() : findCli();
    if (!graphql && cli.isEmpty()) {
        if (!reportedMissingCli) {
            reportedMissingCli = true;
            diagnostic("hardcover-cli-missing");
        }
        return;
    }
    const int link = network ? network() : -1;
    noteNetwork(link, "flush");
    if (link == 0) {
        armNetworkRetry();
        return;
    }
    const int queued = pending.size();
    while (!pending.isEmpty() && (!bookAllowed(pending.first().contentId) ||
                                  recordedPercent(pending.first().contentId) == pending.first().percent))
        pending.removeFirst();
    if (pending.size() != queued)
        persist();
    if (pending.isEmpty())
        return;
    const auto item = pending.first();
    if (graphql) {
        running = true;
        inflightId = item.contentId;
        inflightPercent = item.percent;
        diagnostic("hardcover-start", {{"percent", item.percent}, {"transport", "graphql"}}, false);
        if (retryTimer)
            retryTimer->stop();
        progressClient->update(item.contentId, linkedBook(item.contentId), item.percent,
                               [this](bool ok, const QString &detail) {
                                   CommandResult result;
                                   result.exitCode = ok ? 0 : 1;
                                   result.reason = ok ? QStringLiteral("graphql") : QStringLiteral("graphql-failed");
                                   result.output = detail;
                                   complete(result);
                               });
        return;
    }
    const auto flags = flagsFor(cli);
    running = true;
    inflightId = item.contentId;
    inflightPercent = item.percent;
    QStringList arguments{QStringLiteral("update"), QStringLiteral("--value"), QString::number(item.percent),
                          QStringLiteral("--content-id"), item.contentId};
    const auto bookId = linkedBook(item.contentId);
    if (!bookId.isEmpty() && !flags.linkedFlag.isEmpty())
        arguments << flags.linkedFlag << bookId;
    const auto after = lastSynced(item.contentId);
    if (flags.after && !after.isEmpty())
        arguments << QStringLiteral("--after") << after;
    diagnostic("hardcover-start",
               {{"percent", item.percent},
                {"linked", !bookId.isEmpty() && !flags.linkedFlag.isEmpty()},
                {"after", flags.after && !after.isEmpty()}},
               false);
    if (retryTimer)
        retryTimer->stop();
    runner(this, cli, arguments, [this](const CommandResult &result) {
        complete(result);
    });
}
void HardcoverSync::load() {
    const auto saved = storage::readJson(paths.pendingPath);
    for (const auto &value : saved.value(QStringLiteral("pending")).toArray()) {
        const auto object = value.toObject();
        const auto id = object.value(QStringLiteral("content_id")).toString();
        const int percent = object.value(QStringLiteral("percent")).toInt(-1);
        if (!id.isEmpty() && percent >= hardcoverMinimumPercent && percent <= 100)
            pending.append({id, percent});
    }
    const auto remembered = saved.value(QStringLiteral("sent")).toObject();
    for (auto it = remembered.begin(); it != remembered.end(); ++it) {
        const int percent = it.value().toInt(-1);
        if (percent >= 0)
            sent.insert(it.key(), percent);
    }
}
void HardcoverSync::persist() {
    QJsonArray items;
    for (const auto &item : pending)
        items.append(qt::object({{"content_id", item.contentId}, {"percent", item.percent}}));
    QJsonObject remembered;
    for (auto it = sent.cbegin(); it != sent.cend(); ++it)
        remembered.insert(it.key(), it.value());
    storage::writeJson(paths.pendingPath, qt::object({{"pending", items}, {"sent", remembered}}));
}
bool HardcoverSync::featureEnabled() const {
    return storage::readJson(paths.preferencesPath).value(QStringLiteral("hardcover_progress_sync")).toBool(false);
}
QString HardcoverSync::settingsPath() const {
    return paths.toolDirectory + QStringLiteral("/settings.ini");
}
QString HardcoverSync::bookKey(QString contentId, const char *field) const {
    contentId.replace(QLatin1Char('/'), QLatin1Char('-'));
    contentId.replace(QLatin1Char('\\'), QLatin1Char('-'));
    return contentId + QLatin1Char('/') + QLatin1String(field);
}
bool HardcoverSync::bookAllowed(const QString &contentId) const {
    const auto value = readBookValue(contentId, "enabled");
    if (value.isEmpty() && !QFile::exists(libraryPath()) && !QFile::exists(settingsPath()))
        return true;
    if (value.isNull())
        return true;
    for (const auto &path : {libraryPath(), settingsPath()}) {
        if (!QFile::exists(path))
            continue;
        QSettings settings(path, QSettings::IniFormat);
        const auto key = bookKey(contentId, "enabled");
        if (settings.contains(key))
            return settings.value(key).toBool();
    }
    return true;
}
int HardcoverSync::recordedPercent(const QString &contentId) const {
    if (sent.contains(contentId))
        return sent.value(contentId);
    const auto value = readBookValue(contentId, "progress");
    bool ok = false;
    const int percent = value.toInt(&ok);
    return ok ? percent : -1;
}
QString HardcoverSync::linkedBook(const QString &contentId) const {
    return readBookValue(contentId, "linkedbook");
}
QString HardcoverSync::lastSynced(const QString &contentId) const {
    if (!QFile::exists(settingsPath()))
        return {};
    QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(bookKey(contentId, "lastSynced")).toString();
}
QString HardcoverSync::findCli() const {
    for (const auto *name : {"cli", "CLI"}) {
        const auto path = paths.toolDirectory + QLatin1Char('/') + QLatin1String(name);
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}
bool HardcoverSync::removePending(const QString &contentId) {
    const int before = pending.size();
    for (int index = pending.size() - 1; index >= 0; --index)
        if (pending.at(index).contentId == contentId)
            pending.removeAt(index);
    return pending.size() != before;
}
void HardcoverSync::remember(const QString &contentId, int percent) {
    sent.insert(contentId, percent);
    if (!QFileInfo(paths.toolDirectory).isDir())
        return;
    // Record the percent for NickelHardcover's own debounce. Do not write
    // lastSynced: 0.5.3 rejects the --after flag that value used to add.
    const auto path = QFile::exists(libraryPath()) ? libraryPath() : settingsPath();
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(bookKey(contentId, "progress"), percent);
    settings.sync();
}
void HardcoverSync::complete(const CommandResult &result) {
    const auto id = inflightId;
    const int percent = inflightPercent;
    const int code = result.exitCode;
    running = false;
    inflightId.clear();
    inflightPercent = -1;
    diagnostic("hardcover-result", {{"percent", percent},
                                    {"exit", code},
                                    {"reason", result.reason},
                                    {"stdout", result.output},
                                    {"stderr", result.errorOutput}});
    if (code == 0) {
        remember(id, percent);
        for (int index = 0; index < pending.size(); ++index) {
            if (pending.at(index).contentId == id && pending.at(index).percent == percent) {
                pending.removeAt(index);
                break;
            }
        }
        persist();
        flush();
        return;
    }
    persist();
    armNetworkRetry();
    if (!pending.isEmpty() && (pending.first().contentId != id || pending.first().percent != percent))
        flush();
}
QString HardcoverSync::libraryPath() const {
    return paths.toolDirectory + QStringLiteral("/library.ini");
}
QString HardcoverSync::readBookValue(const QString &contentId, const char *field) const {
    for (const auto &path : {libraryPath(), settingsPath()}) {
        if (!QFile::exists(path))
            continue;
        QSettings settings(path, QSettings::IniFormat);
        const auto key = bookKey(contentId, field);
        if (settings.contains(key))
            return settings.value(key).toString();
    }
    return {};
}
HardcoverSync::CliFlags HardcoverSync::flagsFor(const QString &program) {
    if (probedProgram == program)
        return probedFlags;
    probedProgram = program;
    probedFlags = probe ? probe(program) : probeCli(program);
    diagnostic("hardcover-cli", {{"linked", probedFlags.linkedFlag}, {"after", probedFlags.after}}, false);
    return probedFlags;
}
void HardcoverSync::noteNetwork(int link, const char *source) {
    if (link == lastLink && QString::fromLatin1(source) != QStringLiteral("networkConnected"))
        return;
    lastLink = link;
    diagnostic("hardcover-network",
               {{"state", link == 1   ? "up"
                          : link == 0 ? "down"
                                      : "unknown"},
                {"source", source}},
               false);
}
void HardcoverSync::armNetworkRetry() {
    if (pending.isEmpty() || running) {
        if (retryTimer)
            retryTimer->stop();
        return;
    }
    if (!retryTimer) {
        retryTimer = new QTimer(this);
        retryTimer->setInterval(5000);
        connect(retryTimer, &QTimer::timeout, this, [this] {
            const int link = network ? network() : -1;
            if (link == lastLink)
                return;
            noteNetwork(link, "poll");
            if (link != 0)
                flush();
        });
    }
    if (!retryTimer->isActive())
        retryTimer->start();
}
void HardcoverSync::watchNetwork() {
    if (watchingNetwork || networkWatchTried)
        return;
    using Manager = QObject *(*)();
    const auto manager = reinterpret_cast<Manager>(dlsym(RTLD_DEFAULT, "_ZN15WirelessManager14sharedInstanceEv"));
    auto *instance = manager ? manager() : nullptr;
    if (!manager) {
        diagnostic("hardcover-network-watch", {{"connected", false}, {"reason", "symbol-missing"}});
        networkWatchTried = true;
        return;
    }
    if (!instance)
        return;
    networkWatchTried = true;
    watchingNetwork = bool(connect(instance, SIGNAL(networkConnected()), this, SLOT(networkConnected())));
    diagnostic("hardcover-network-watch", {{"connected", watchingNetwork}});
}
void HardcoverSync::networkConnected() {
    noteNetwork(1, "networkConnected");
    flush();
}
}
