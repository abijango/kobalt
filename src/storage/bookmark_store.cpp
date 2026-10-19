#include "bookmark_store.h"
#include "reader_storage.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace kobalt {
namespace {
constexpr int formatVersion = 1;
constexpr int maxFolders = 10000;

QJsonObject readObject(const QString &path, QString *error) {
    const auto read = storage::readJsonFile(path, BookmarkStore::maxFileBytes);
    const char *problem = nullptr;
    switch (read.status) {
    case storage::ReadStatus::Ok:
    case storage::ReadStatus::Missing:
        break;
    case storage::ReadStatus::Unreadable:
        problem = "unreadable";
        break;
    case storage::ReadStatus::TooLarge:
        problem = "too-large";
        break;
    case storage::ReadStatus::Invalid:
        problem = "invalid";
        break;
    }
    if (problem && error)
        *error = QString::fromLatin1(problem);
    return read.object;
}
bool writeObject(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

QJsonObject bookmarkJson(const Bookmark &bookmark) {
    QJsonObject object;
    object["position"] = bookmark.position;
    object["chapter"] = bookmark.chapter;
    object["excerpt"] = bookmark.excerpt;
    object["created"] = bookmark.created.toUTC().toString(Qt::ISODate);
    return object;
}
bool bookmarkFromJson(const QJsonObject &object, Bookmark *bookmark) {
    const auto position = object.value("position").toString();
    if (position.isEmpty() || position.size() > 4096)
        return false;
    bookmark->position = position;
    bookmark->chapter = object.value("chapter").toString().left(512);
    bookmark->excerpt = object.value("excerpt").toString().left(512);
    bookmark->created = QDateTime::fromString(object.value("created").toString(), Qt::ISODate).toUTC();
    return true;
}

QString BookmarkStore::bookKey(const QString &identity, const QByteArray &fingerprint) {
    static const QRegularExpression legacyUuid(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    // Keep existing UUID folders. Other server IDs get a case-sensitive hash
    // so their length and filename characters cannot affect annotation paths.
    if (legacyUuid.match(identity).hasMatch())
        return "kobo-" + identity.toLower();
    if (identity.startsWith("file://") && digest.match(QString::fromLatin1(fingerprint)).hasMatch())
        return "file-" + QString::fromLatin1(fingerprint);
    if (!identity.isEmpty() && identity != "." && identity != ".." && !identity.contains('/') &&
        !identity.contains('\\') && !identity.contains(QChar(0))) {
        return "kobo-id-" +
               QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    return {};
}

BookmarkStore::BookmarkStore(QString root, QString identity, QByteArray fingerprint, QString title)
    : root_(std::move(root)), identity_(std::move(identity)), title_(std::move(title)),
      key_(bookKey(identity_, fingerprint)), fingerprint_(std::move(fingerprint)) {}

QString BookmarkStore::folder() const {
    return key_.isEmpty() ? QString() : root_ + '/' + key_;
}

bool BookmarkStore::readFolder(const QString &folder, QString *error) {
    QString problem;
    const auto object = readObject(folder + "/bookmarks.json", &problem);
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return false;
    }
    // An explicit list with the wrong type is damaged data, not an empty list.
    // Preserve its bytes by refusing edits after this failed load.
    const auto list = object.value("bookmarks");
    if (!list.isUndefined() && !list.isArray()) {
        if (error)
            *error = "invalid";
        return false;
    }
    QVector<Bookmark> loaded;
    for (const auto &value : list.toArray()) {
        Bookmark bookmark;
        if (loaded.size() < maxBookmarks && bookmarkFromJson(value.toObject(), &bookmark))
            loaded.append(bookmark);
    }
    bookmarks_ = loaded;
    return true;
}

bool BookmarkStore::load(QString *error) {
    bookmarks_.clear();
    adoptedFrom_.clear();
    loadFailed_ = true;
    if (key_.isEmpty()) {
        if (error)
            *error = "no-stable-key";
        return false;
    }
    if (QFile::exists(folder() + "/bookmarks.json")) {
        loadFailed_ = !readFolder(folder(), error);
        return !loadFailed_;
    }
    // A sideloaded file that was edited has a new fingerprint. Keep the
    // bookmarks of the folder that last saw this path.
    if (!identity_.startsWith("file://")) {
        loadFailed_ = false;
        return true;
    }
    const auto folders =
        QDir(root_).entryList(QStringList() << "file-*", QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QString newest;
    QDateTime modified;
    for (int i = 0; i < folders.size() && i < maxFolders; ++i) {
        const auto candidate = root_ + '/' + folders[i];
        if (folders[i] == key_)
            continue;
        if (readObject(candidate + "/book.json", nullptr).value("identity").toString() != identity_)
            continue;
        const auto saved = QFileInfo(candidate + "/bookmarks.json").lastModified();
        if (newest.isEmpty() || saved > modified) {
            newest = folders[i];
            modified = saved;
        }
    }
    if (!newest.isEmpty()) {
        if (!readFolder(root_ + '/' + newest, error))
            return false;
        adoptedFrom_ = newest;
    }
    loadFailed_ = false;
    return true;
}

bool BookmarkStore::save(QString *error) {
    // A failed read is not an empty list. Keep the old file for recovery.
    if (loadFailed_) {
        if (error)
            *error = "load-failed";
        return false;
    }
    if (key_.isEmpty()) {
        if (error)
            *error = "no-stable-key";
        return false;
    }
    if (!QDir().mkpath(folder())) {
        if (error)
            *error = "create-directory";
        return false;
    }
    QJsonObject book;
    book["version"] = formatVersion;
    book["identity"] = identity_;
    book["title"] = title_;
    book["fingerprint"] = QString::fromLatin1(fingerprint_);
    QJsonArray list;
    for (const auto &bookmark : bookmarks_)
        list.append(bookmarkJson(bookmark));
    QJsonObject marks;
    marks["version"] = formatVersion;
    marks["bookmarks"] = list;
    const auto marksBytes = QJsonDocument(marks).toJson();
    const auto bookBytes = QJsonDocument(book).toJson();
    // Check both before replacing either file. A successful save must remain
    // readable by load(), and an oversized edit must keep the previous list.
    if (marksBytes.size() > maxFileBytes || bookBytes.size() > maxFileBytes) {
        if (error)
            *error = "storage-limit";
        return false;
    }
    // The list is authoritative. A metadata failure must leave its previous
    // bytes intact; metadata can safely be refreshed before the list commits.
    if (!writeObject(folder() + "/book.json", bookBytes)) {
        if (error)
            *error = "write-metadata";
        return false;
    }
    if (!writeObject(folder() + "/bookmarks.json", marksBytes)) {
        if (error)
            *error = "write-bookmarks";
        return false;
    }
    adoptedFrom_.clear();
    return true;
}

void BookmarkStore::setBookmarks(QVector<Bookmark> bookmarks) {
    if (bookmarks.size() > maxBookmarks)
        bookmarks.resize(maxBookmarks);
    bookmarks_ = std::move(bookmarks);
}

QString excerptAfterChapter(const QString &excerpt, const QString &chapter) {
    const auto text = excerpt.simplified(), title = chapter.simplified();
    // The title must end at a space, so "Chapter 1" does not match "Chapter 10".
    if (title.isEmpty() || text.size() <= title.size() + 1 || !text.startsWith(title, Qt::CaseInsensitive) ||
        !text.at(title.size()).isSpace())
        return excerpt;
    return text.mid(title.size() + 1);
}

bool BookmarkStore::merge(const std::function<int(const QString &)> &page,
                          const std::function<Bookmark(int, const QDateTime &)> &atPage) {
    QMap<int, QVector<Bookmark>> pages;
    QVector<Bookmark> unresolved;
    for (const auto &bookmark : bookmarks_) {
        const int number = page(bookmark.position);
        if (number < 0)
            unresolved.append(bookmark);
        else
            pages[number].append(bookmark);
    }
    QVector<Bookmark> result;
    bool changed = false;
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        if (it.value().size() == 1) {
            result.append(it.value().first());
            continue;
        }
        QDateTime oldest = it.value().first().created;
        for (const auto &bookmark : it.value())
            if (bookmark.created.isValid() && (!oldest.isValid() || bookmark.created < oldest))
                oldest = bookmark.created;
        result.append(atPage(it.key(), oldest));
        changed = true;
    }
    // Keep the stored order when nothing merged.
    if (!changed)
        return false;
    bookmarks_ = result + unresolved;
    return true;
}
}
