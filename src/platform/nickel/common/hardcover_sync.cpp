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
void runDeviceCommand(QObject *parent, const QString &program, const QStringList &arguments,
                      std::function<void(int)> finished) {
    auto *process = new QProcess(parent);
    auto completed = std::make_shared<bool>(false);
    auto finish = [process, finished, completed](int code) {
        if (*completed)
            return;
        *completed = true;
        const auto output = redactedOutput(process->readAllStandardOutput());
        if (!output.isEmpty())
            diagnostic("hardcover-output", {{"text", output}}, false);
        process->deleteLater();
        finished(code);
    };
    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                     [finish](int code, QProcess::ExitStatus status) {
                         finish(status == QProcess::NormalExit ? code : -1);
                     });
    process->start(program, arguments);
    // start() reports failure on the next turn when the command cannot be launched.
    qt::later(0, process, [process, finish] {
        if (process->state() == QProcess::NotRunning)
            finish(process->error() == QProcess::UnknownError ? process->exitCode() : -1);
    });
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
HardcoverSync::HardcoverSync(Paths paths, std::function<int()> network, Runner runner, QObject *parent)
    : QObject(parent), paths(std::move(paths)), network(std::move(network)), runner(std::move(runner)) {
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
    deviceInstance = new HardcoverSync(std::move(paths), deviceNetwork, runDeviceCommand, qApp);
}
HardcoverSync *HardcoverSync::installed() {
    return deviceInstance;
}
void HardcoverSync::note(const QString &contentId, int percent) {
    if (!featureEnabled())
        return;
    // An empty id or a negative percent only retries updates already queued.
    if (contentId.isEmpty() || percent < 0) {
        flush();
        return;
    }
    if (percent < hardcoverMinimumPercent || percent > 100 || !bookAllowed(contentId) ||
        recordedPercent(contentId) == percent) {
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
    const auto cli = findCli();
    if (cli.isEmpty()) {
        if (!reportedMissingCli) {
            reportedMissingCli = true;
            diagnostic("hardcover-cli-missing");
        }
        return;
    }
    const int link = network ? network() : -1;
    if (link == 0)
        return;
    const int queued = pending.size();
    while (!pending.isEmpty() && (!bookAllowed(pending.first().contentId) ||
                                  recordedPercent(pending.first().contentId) == pending.first().percent))
        pending.removeFirst();
    if (pending.size() != queued)
        persist();
    if (pending.isEmpty())
        return;
    const auto item = pending.first();
    running = true;
    inflightId = item.contentId;
    inflightPercent = item.percent;
    QStringList arguments{QStringLiteral("update"), QStringLiteral("--content-id"), item.contentId,
                          QStringLiteral("--value"), QString::number(item.percent)};
    const auto bookId = linkedBook(item.contentId);
    if (!bookId.isEmpty())
        arguments << QStringLiteral("--book-id") << bookId;
    const auto after = lastSynced(item.contentId);
    if (!after.isEmpty())
        arguments << QStringLiteral("--after") << after;
    diagnostic("hardcover-start", {{"percent", item.percent}}, false);
    runner(this, cli, arguments, [this](int code) {
        complete(code);
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
    if (!QFile::exists(settingsPath()))
        return true;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    const auto key = bookKey(contentId, "enabled");
    if (!settings.contains(key))
        return true;
    return settings.value(key).toBool();
}
int HardcoverSync::recordedPercent(const QString &contentId) const {
    if (sent.contains(contentId))
        return sent.value(contentId);
    if (!QFile::exists(settingsPath()))
        return -1;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    const auto key = bookKey(contentId, "progress");
    if (!settings.contains(key))
        return -1;
    return settings.value(key).toInt();
}
QString HardcoverSync::linkedBook(const QString &contentId) const {
    if (!QFile::exists(settingsPath()))
        return {};
    QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(bookKey(contentId, "linkedbook")).toString();
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
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(bookKey(contentId, "progress"), percent);
    settings.setValue(bookKey(contentId, "lastSynced"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.sync();
}
void HardcoverSync::complete(int code) {
    const auto id = inflightId;
    const int percent = inflightPercent;
    running = false;
    inflightId.clear();
    inflightPercent = -1;
    diagnostic("hardcover-result", {{"percent", percent}, {"exit", code}});
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
    if (!pending.isEmpty() && (pending.first().contentId != id || pending.first().percent != percent))
        flush();
}
void HardcoverSync::watchNetwork() {
    if (watchingNetwork || networkWatchTried)
        return;
    using Manager = QObject *(*)();
    const auto manager = reinterpret_cast<Manager>(dlsym(RTLD_DEFAULT, "_ZN15WirelessManager14sharedInstanceEv"));
    auto *instance = manager ? manager() : nullptr;
    if (!instance)
        return;
    networkWatchTried = true;
    watchingNetwork = bool(connect(instance, SIGNAL(networkConnected()), this, SLOT(flush())));
    if (!watchingNetwork)
        diagnostic("hardcover-network-watch-failed");
}
}
