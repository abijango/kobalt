#pragma once
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

namespace kobalt {
// One read-through, oldest id first. Hardcover's currently-reading progress and
// KOReader's plugin both use the last of these: the highest id.
struct HardcoverRead {
    qint64 id = 0;
    qint64 editionId = 0;
    int editionPages = 0;
    QString startedAt;
};
// The update KOReader sends. progress_pages is the page count the site shows.
// The GraphQL input has no percentage field.
struct HardcoverProgressUpdate {
    bool insert = false;
    qint64 userBookId = 0;
    qint64 readId = 0;
    qint64 editionId = 0;
    int progressPages = 0;
    QString startedAt;
    bool valid() const {
        return editionId > 0 && progressPages > 0 && (insert ? userBookId > 0 : readId > 0);
    }
};
// The user book the site is showing. status Currently Reading wins over other copies.
struct HardcoverUserBook {
    qint64 id = 0;
    qint64 editionId = 0;
    int pages = 0;
    QVector<HardcoverRead> reads;
};

// Token text from NickelHardcover's config.ini. The raw token is not logged.
QString hardcoverToken(const QString &configText);
// Filesystem path for a Nickel ContentID. Sideloaded IDs keep literal #, ?
// and percent characters. Other IDs are filenames in Nickel's kepub directory.
QString hardcoverBookPath(const QString &contentId);
HardcoverUserBook hardcoverSelectUserBook(const QJsonArray &books);
// `reads` is ordered by id. Page count is the percent of that read's edition.
HardcoverProgressUpdate hardcoverProgressUpdate(const QVector<HardcoverRead> &reads, qint64 userBookId,
                                                qint64 fallbackEditionId, int editionPages, int percent);

// Sends reading progress with the Hardcover GraphQL API, the same mutation
// KOReader's plugin uses. The token is read from NickelHardcover's config and
// is never written to the log.
class HardcoverProgress final : public QObject {
    Q_OBJECT
public:
    explicit HardcoverProgress(QString configPath, QObject *parent = nullptr);
    bool configured() const;
    void update(const QString &contentId, const QString &linkedId, int percent,
                std::function<void(bool ok, QString detail)> finished);

private:
    QString configPath;
    QString token() const;
};
}
