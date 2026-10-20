#pragma once
#include "hardcover_progress.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>
class QTimer;

namespace kobalt {
inline constexpr int hardcoverMinimumPercent = 2;

// Optional Hardcover.app progress updates. When NickelHardcover's config.ini
// has a token, Kobalt calls Hardcover's GraphQL API and does not log that
// token. Kobalt deliberately does not run NickelHardcover's update command:
// it can create a user-book read. Calls return immediately: a missing network
// leaves the update queued and does not delay suspend.
class HardcoverSync : public QObject {
    Q_OBJECT
public:
    struct Paths {
        QString toolDirectory;
        QString pendingPath;
        QString preferencesPath;
    };
    // 1 when the network can reach Hardcover, 0 when it cannot, -1 when that
    // is not known.
    HardcoverSync(Paths paths, std::function<int()> network, QObject *parent = nullptr);
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

    void load();
    void persist();
    bool featureEnabled() const;
    bool bookAllowed(const QString &contentId) const;
    int recordedPercent(const QString &contentId) const;
    QString linkedBook(const QString &contentId) const;
    QString bookKey(QString contentId, const char *field) const;
    QString settingsPath() const;
    bool removePending(const QString &contentId);
    void remember(const QString &contentId, int percent);
    void complete(bool ok, bool skipped, const QString &reason, const QString &detail);
    void watchNetwork();
    void noteNetwork(int link, const char *source);
    void armNetworkRetry();
    QString libraryPath() const;
    QString readBookValue(const QString &contentId, const char *field) const;
};
}
