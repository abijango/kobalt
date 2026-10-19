#pragma once
#include "hardcover_progress.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
class QTimer;

namespace kobalt {
inline constexpr int hardcoverMinimumPercent = 2;

// Optional Hardcover.app progress updates. When NickelHardcover's config.ini
// has a token, Kobalt calls Hardcover's GraphQL API and does not log that
// token. Otherwise it runs the installed NickelHardcover command. When neither
// is available, or the reader setting is off, nothing is sent. Calls return
// immediately: a missing network leaves the update queued and does not delay
// suspend.
class HardcoverSync : public QObject {
    Q_OBJECT
public:
    struct Paths {
        QString toolDirectory;
        QString pendingPath;
        QString preferencesPath;
    };
    // Flags the installed command accepts. NickelHardcover 0.5 uses --linked-id.
    // 0.4.2 uses --book-id and --after. Passing an unknown flag exits 1.
    struct CliFlags {
        QString linkedFlag;
        bool after = false;
    };
    struct CommandResult {
        int exitCode = -1;
        QString reason;
        QString output;
        QString errorOutput;
    };
    // 1 when the network can reach Hardcover, 0 when it cannot, -1 when that
    // is not known. The runner owns the process and reports how it finished.
    using Runner = std::function<void(QObject *parent, const QString &program, const QStringList &arguments,
                                      std::function<void(const CommandResult &)> finished)>;
    using Probe = std::function<CliFlags(const QString &program)>;

    HardcoverSync(Paths paths, std::function<int()> network, Runner runner, Probe probe = {},
                  QObject *parent = nullptr);
    void note(const QString &contentId, int percent);
    int pendingCount() const {
        return pending.size();
    }
    bool busy() const {
        return running;
    }

    static void install(const QString &preferencesPath);
    static HardcoverSync *installed();

public slots:
    void flush();
    void networkConnected();

private:
    struct Update {
        QString contentId;
        int percent = 0;
    };
    Paths paths;
    std::function<int()> network;
    Runner runner;
    Probe probe;
    QString probedProgram;
    CliFlags probedFlags;
    int lastLink = -2;
    QTimer *retryTimer = nullptr;
    HardcoverProgress *progressClient = nullptr;
    QVector<Update> pending;
    QHash<QString, int> sent;
    QString inflightId;
    int inflightPercent = -1;
    bool running = false;
    bool watchingNetwork = false;
    bool networkWatchTried = false;
    bool reportedMissingCli = false;

    void load();
    void persist();
    bool featureEnabled() const;
    bool bookAllowed(const QString &contentId) const;
    int recordedPercent(const QString &contentId) const;
    QString linkedBook(const QString &contentId) const;
    QString lastSynced(const QString &contentId) const;
    QString findCli() const;
    QString bookKey(QString contentId, const char *field) const;
    QString settingsPath() const;
    bool removePending(const QString &contentId);
    void remember(const QString &contentId, int percent);
    void complete(const CommandResult &result);
    void watchNetwork();
    void noteNetwork(int link, const char *source);
    void armNetworkRetry();
    CliFlags flagsFor(const QString &program);
    QString libraryPath() const;
    QString readBookValue(const QString &contentId, const char *field) const;
};
}
