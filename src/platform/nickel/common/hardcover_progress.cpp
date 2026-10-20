#include "hardcover_progress.h"

#include "local_book_path.h"
#include "publication/zip_resource.h"
#include "support/diagnostics.h"
#include "support/qt_compat.h"

#include <QDate>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtMath>

#include <algorithm>

namespace kobalt {
namespace {
const auto apiUrl = QStringLiteral("https://api.hardcover.app/v1/graphql");
const auto updateMutation = QStringLiteral(
    "mutation UpdateBookProgress($id: Int!, $pages: Int, $editionId: Int, $startedAt: date) { "
    "update_user_book_read(id: $id, object: { progress_pages: $pages, edition_id: $editionId, started_at: "
    "$startedAt }) { error user_book_read { id progress_pages edition_id } } }");
QString zipText(const QString &path, const QString &name) {
    ZipResource zip;
    if (!zip.open(path, name, 2 * 1024 * 1024))
        return {};
    QByteArray bytes;
    while (!zip.finished() && bytes.size() < 2 * 1024 * 1024) {
        const auto chunk = zip.next();
        if (chunk.isEmpty())
            break;
        bytes += chunk;
    }
    return QString::fromUtf8(bytes);
}
QString opfPath(const QString &container) {
    const int marker = container.indexOf(QStringLiteral("full-path"));
    if (marker < 0)
        return {};
    const int quote = container.indexOf(QLatin1Char('"'), marker);
    const int end = quote < 0 ? -1 : container.indexOf(QLatin1Char('"'), quote + 1);
    if (quote < 0 || end < 0)
        return {};
    return container.mid(quote + 1, end - quote - 1);
}
QStringList isbnFromText(const QString &text) {
    QStringList found;
    QString digits;
    auto consider = [&] {
        if (digits.size() == 13 &&
            (digits.startsWith(QStringLiteral("978")) || digits.startsWith(QStringLiteral("979"))) &&
            !found.contains(digits))
            found.append(digits);
        digits.clear();
    };
    for (int index = 0; index < text.size(); ++index) {
        const auto character = text.at(index);
        if (character.isDigit()) {
            digits += character;
            if (digits.size() > 13)
                digits.clear();
        } else if (character == QLatin1Char('-') && !digits.isEmpty()) {
            continue;
        } else {
            consider();
        }
    }
    consider();
    return found;
}
QStringList epubIsbns(const QString &path) {
    const auto container = zipText(path, QStringLiteral("META-INF/container.xml"));
    const auto opf = opfPath(container);
    if (opf.isEmpty())
        return {};
    return isbnFromText(zipText(path, opf));
}
QJsonValue meId(const QJsonObject &data) {
    const auto me = data.value(QStringLiteral("me"));
    if (me.isArray() && !me.toArray().isEmpty())
        return me.toArray().first().toObject().value(QStringLiteral("id"));
    return me.toObject().value(QStringLiteral("id"));
}
qint64 jsonId(const QJsonValue &value) {
    return qint64(value.toDouble());
}
QJsonValue gqlInt(qint64 value) {
    return QJsonValue(int(value));
}
int editionPages(const QJsonObject &object) {
    const auto nested = object.value(QStringLiteral("edition")).toObject().value(QStringLiteral("pages")).toInt();
    return nested > 0 ? nested : object.value(QStringLiteral("pages")).toInt();
}
HardcoverRead readFromJson(const QJsonObject &object) {
    HardcoverRead read;
    read.id = jsonId(object.value(QStringLiteral("id")));
    read.editionId = jsonId(object.value(QStringLiteral("edition_id")));
    read.editionPages = editionPages(object);
    read.startedAt = object.value(QStringLiteral("started_at")).toString();
    return read;
}
QString redact(const QString &secret, QString text) {
    if (!secret.isEmpty())
        text.replace(secret, QStringLiteral("redacted"));
    text = text.simplified();
    if (text.contains(QStringLiteral("Bearer"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("authorization"), Qt::CaseInsensitive))
        return QStringLiteral("redacted");
    return text.left(160);
}
}
QString hardcoverToken(const QString &configText) {
    QString accessToken;
    QString authorization;
    for (QString line : configText.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')) ||
            line.startsWith(QLatin1Char('[')))
            continue;
        const int split = line.indexOf(QLatin1Char('='));
        if (split <= 0)
            continue;
        const auto key = line.left(split).trimmed().toLower();
        auto value = line.mid(split + 1).trimmed();
        if (value.size() >= 2 && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
                                  (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))))
            value = value.mid(1, value.size() - 2).trimmed();
        if (key == QStringLiteral("access_token"))
            accessToken = value;
        else if (key == QStringLiteral("authorization"))
            authorization = value;
    }
    auto token = accessToken.isEmpty() ? authorization : accessToken;
    if (token.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
        token = token.mid(7).trimmed();
    return token;
}
QString hardcoverBookPath(const QString &contentId) {
    const auto local = localReaderBookPath(contentId);
    return local.isEmpty() ? downloadedReaderBookPath(contentId) : local;
}
HardcoverUserBook hardcoverSelectUserBook(const QJsonArray &books) {
    HardcoverUserBook chosen;
    for (const auto &value : books) {
        const auto book = value.toObject();
        if (book.value(QStringLiteral("status_id")).toInt() != 2)
            continue;
        if (chosen.id == 0) {
            chosen.id = jsonId(book.value(QStringLiteral("id")));
            chosen.editionId = jsonId(book.value(QStringLiteral("edition_id")));
            chosen.pages = editionPages(book);
        }
        for (const auto &readValue : book.value(QStringLiteral("user_book_reads")).toArray()) {
            const auto read = readFromJson(readValue.toObject());
            if (read.id > 0)
                chosen.reads.append(read);
        }
    }
    std::sort(chosen.reads.begin(), chosen.reads.end(), [](const HardcoverRead &left, const HardcoverRead &right) {
        return left.id < right.id;
    });
    return chosen;
}
HardcoverProgressUpdate hardcoverProgressUpdate(const QVector<HardcoverRead> &reads, qint64 fallbackEditionId,
                                                int editionPages, int percent) {
    HardcoverProgressUpdate update;
    if (reads.size() != 1)
        return update;
    int pages = editionPages;
    const auto &current = reads.first();
    update.readId = current.id;
    update.editionId = current.editionId > 0 ? current.editionId : fallbackEditionId;
    update.startedAt = current.startedAt;
    if (current.editionPages > 0)
        pages = current.editionPages;
    if (update.startedAt.isEmpty())
        update.startedAt = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (pages > 0 && percent > 0)
        update.progressPages = qBound(1, qRound(percent * (pages / 100.0)), pages);
    return update;
}
HardcoverProgress::HardcoverProgress(QString configPath, QObject *parent)
    : QObject(parent), configPath(std::move(configPath)) {}
QString HardcoverProgress::token() const {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return hardcoverToken(QString::fromUtf8(file.readAll()));
}
bool HardcoverProgress::configured() const {
    return !token().isEmpty();
}
void HardcoverProgress::update(const QString &contentId, const QString &linkedId, int percent,
                               std::function<void(bool, QString)> finished) {
    const auto secret = token();
    if (secret.isEmpty()) {
        finished(false, QStringLiteral("no-token"));
        return;
    }
    auto *network = new QNetworkAccessManager(this);
    auto fail = [finished, network, secret](const QString &reason) {
        const auto safe = redact(secret, reason);
        diagnostic("hardcover-graphql", {{"failed", true}, {"reason", safe}});
        finished(false, safe);
        network->deleteLater();
    };
    auto post = [network, secret](const QString &query, const QJsonObject &variables,
                                  const std::function<void(QJsonObject, QString)> &done) {
        QNetworkRequest request{QUrl(apiUrl)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QByteArray("Bearer ") + secret.toUtf8());
        request.setRawHeader("User-Agent", "Kobalt hardcover progress");
        const auto body = QJsonDocument(qt::object({{"query", query}, {"variables", variables}})).toJson();
        auto *reply = network->post(request, body);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done, secret] {
            const auto error = reply->error();
            const auto payload = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (error != QNetworkReply::NoError && !payload.contains(QStringLiteral("data"))) {
                done({}, QStringLiteral("network"));
                return;
            }
            if (payload.contains(QStringLiteral("errors"))) {
                const auto errors = payload.value(QStringLiteral("errors")).toArray();
                const auto message = errors.isEmpty()
                                         ? QStringLiteral("graphql")
                                         : errors.first().toObject().value(QStringLiteral("message")).toString();
                done({}, redact(secret, message));
                return;
            }
            done(payload.value(QStringLiteral("data")).toObject(), {});
        });
    };
    const auto path = hardcoverBookPath(contentId);
    const auto isbns = epubIsbns(path);
    auto resolveEdition = [post, fail, isbns, percent, finished, network, linkedId](qint64 userId) {
        auto withEdition = [post, fail, percent, finished, network, userId](qint64 editionId, qint64 bookId,
                                                                            int pages) {
            if (bookId <= 0)
                return fail(QStringLiteral("edition-not-found"));
            const auto readsQuery = QStringLiteral(
                "query ($bookId: Int!, $userId: Int!) { user_books(where: {book_id: {_eq: $bookId}, user_id: {_eq: "
                "$userId}, status_id: {_eq: 2}}) { id edition_id status_id edition { pages } user_book_reads(where: "
                "{finished_at: {_is_null: true}, edition: {reading_format_id: {_neq: 2}}}, order_by: {id: asc}) { "
                "id started_at edition_id edition { pages } } } }");
            post(readsQuery, qt::object({{"bookId", gqlInt(bookId)}, {"userId", gqlInt(userId)}}),
                 [post, fail, percent, finished, network, editionId, pages](const QJsonObject &data,
                                                                            const QString &error) {
                     if (!error.isEmpty())
                         return fail(error);
                     const auto chosen = hardcoverSelectUserBook(data.value(QStringLiteral("user_books")).toArray());
                     if (chosen.reads.size() != 1) {
                         const auto reason = chosen.reads.isEmpty() ? QStringLiteral("no-active-read")
                                                                    : QStringLiteral("ambiguous-active-read");
                         diagnostic("hardcover-graphql",
                                    {{"skipped", true}, {"reason", reason}, {"active_reads", chosen.reads.size()}});
                         return finished(true, QStringLiteral("skipped-%1").arg(reason));
                     }
                     const auto &read = chosen.reads.first();
                     const qint64 readEdition = read.editionId > 0     ? read.editionId
                                                : chosen.editionId > 0 ? chosen.editionId
                                                                       : editionId;
                     int readPages = 0;
                     if (read.editionId == readEdition && read.editionPages > 0)
                         readPages = read.editionPages;
                     else if (chosen.editionId == readEdition && chosen.pages > 0)
                         readPages = chosen.pages;
                     else if (editionId == readEdition && pages > 0)
                         readPages = pages;
                     auto send = [post, fail, finished, network, chosen, readEdition, percent](int editionPageCount) {
                         auto reads = chosen.reads;
                         if (reads.first().editionPages <= 0)
                             reads.first().editionPages = editionPageCount;
                         const auto update = hardcoverProgressUpdate(reads, readEdition, editionPageCount, percent);
                         if (!update.valid())
                             return fail(QStringLiteral("no-pages"));
                         post(updateMutation,
                              qt::object({{"id", gqlInt(update.readId)},
                                          {"pages", update.progressPages},
                                          {"editionId", gqlInt(update.editionId)},
                                          {"startedAt", update.startedAt}}),
                              [fail, finished, network, update, chosen](const QJsonObject &result,
                                                                        const QString &mutationError) {
                                  if (!mutationError.isEmpty())
                                      return fail(mutationError);
                                  const auto payload = result.value(QStringLiteral("update_user_book_read")).toObject();
                                  const auto apiError = payload.value(QStringLiteral("error"));
                                  const auto apiErrorText =
                                      apiError.isArray() ? apiError.toArray().first().toString() : apiError.toString();
                                  if (!apiErrorText.isEmpty())
                                      return fail(apiErrorText);
                                  const auto stored = payload.value(QStringLiteral("user_book_read")).toObject();
                                  if (jsonId(stored.value(QStringLiteral("id"))) <= 0)
                                      return fail(QStringLiteral("empty-read"));
                                  const auto detail = QStringLiteral("read %1 edition %2 pages %3")
                                                          .arg(jsonId(stored.value(QStringLiteral("id"))))
                                                          .arg(jsonId(stored.value(QStringLiteral("edition_id"))))
                                                          .arg(stored.value(QStringLiteral("progress_pages")).toInt());
                                  diagnostic("hardcover-graphql", {{"read_id", update.readId},
                                                                   {"edition_id", update.editionId},
                                                                   {"user_book_edition", chosen.editionId},
                                                                   {"progress_pages", update.progressPages},
                                                                   {"active_reads", chosen.reads.size()}});
                                  finished(true, detail);
                                  network->deleteLater();
                              });
                     };
                     if (readPages > 0)
                         return send(readPages);
                     if (readEdition <= 0)
                         return fail(QStringLiteral("no-pages"));
                     post(QStringLiteral("query ($id: Int!) { editions_by_pk(id: $id) { pages } }"),
                          qt::object({{"id", gqlInt(readEdition)}}),
                          [send, fail](const QJsonObject &editionData, const QString &editionError) {
                              if (!editionError.isEmpty())
                                  return fail(editionError);
                              send(editionData.value(QStringLiteral("editions_by_pk"))
                                       .toObject()
                                       .value(QStringLiteral("pages"))
                                       .toInt());
                          });
                 });
        };
        const bool linkedNumeric = !linkedId.isEmpty() && linkedId.toLongLong() > 0;
        auto fromIsbn = [post, withEdition, fail, isbns] {
            if (isbns.isEmpty())
                return fail(QStringLiteral("no-isbn"));
            const auto isbn = isbns.first();
            const auto editionQuery = QStringLiteral(
                "query ($isbn: String!) { editions(where: {_or: [{isbn_13: {_eq: $isbn}}, {isbn_10: {_eq: $isbn}}]}, "
                "limit: 1) { id book_id pages } }");
            post(editionQuery, qt::object({{"isbn", isbn}}),
                 [withEdition, fail](const QJsonObject &data, const QString &error) {
                     if (!error.isEmpty())
                         return fail(error);
                     const auto editions = data.value(QStringLiteral("editions")).toArray();
                     if (editions.isEmpty())
                         return fail(QStringLiteral("edition-not-found"));
                     const auto edition = editions.first().toObject();
                     withEdition(jsonId(edition.value(QStringLiteral("id"))),
                                 jsonId(edition.value(QStringLiteral("book_id"))),
                                 edition.value(QStringLiteral("pages")).toInt());
                 });
        };
        if (!linkedNumeric)
            return fromIsbn();
        post(QStringLiteral(
                 "query ($id: Int!) { editions_by_pk(id: $id) { id book_id pages } books_by_pk(id: $id) { id } }"),
             qt::object({{"id", gqlInt(linkedId.toLongLong())}}),
             [withEdition, fromIsbn, fail](const QJsonObject &data, const QString &error) {
                 if (!error.isEmpty())
                     return fail(error);
                 const auto edition = data.value(QStringLiteral("editions_by_pk")).toObject();
                 if (!edition.isEmpty())
                     return withEdition(jsonId(edition.value(QStringLiteral("id"))),
                                        jsonId(edition.value(QStringLiteral("book_id"))),
                                        edition.value(QStringLiteral("pages")).toInt());
                 const auto bookId =
                     jsonId(data.value(QStringLiteral("books_by_pk")).toObject().value(QStringLiteral("id")));
                 if (bookId > 0)
                     return withEdition(0, bookId, 0);
                 fromIsbn();
             });
    };
    post(QStringLiteral("query { me { id } }"), {},
         [resolveEdition, fail](const QJsonObject &data, const QString &error) {
             if (!error.isEmpty())
                 return fail(error);
             const auto id = jsonId(meId(data));
             if (id <= 0)
                 return fail(QStringLiteral("no-user"));
             resolveEdition(id);
         });
}
}
