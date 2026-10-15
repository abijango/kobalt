#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <utility>

namespace kobalt {
// Change only the calling thread's connection, and restore Nickel's setting
// before releasing it. Progress saves must not wait for another writer.
class NativeDatabaseWait {
public:
    NativeDatabaseWait(QSqlDatabase connection, bool nonblocking) : connection(std::move(connection)) {
        if (!nonblocking) {
            return;
        }
        QSqlQuery query(this->connection);
        ready_ = query.exec(QStringLiteral("PRAGMA busy_timeout")) && query.next();
        if (!ready_) {
            return;
        }
        previous = query.value(0).toInt();
        ready_ = query.exec(QStringLiteral("PRAGMA busy_timeout=0"));
    }
    ~NativeDatabaseWait() {
        if (previous >= 0) {
            QSqlQuery restore(connection);
            restore.exec(QStringLiteral("PRAGMA busy_timeout=%1").arg(previous));
        }
    }
    NativeDatabaseWait(const NativeDatabaseWait &) = delete;
    NativeDatabaseWait &operator=(const NativeDatabaseWait &) = delete;
    bool ready() const {
        return ready_;
    }

private:
    QSqlDatabase connection;
    int previous = -1;
    bool ready_ = true;
};
}
