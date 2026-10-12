#pragma once
#include "chapter.h"
#include "reading_progress.h"
#include <functional>

namespace kobalt {
struct ReadingChapterData {
    QVector<ChapterTarget> contents, graph;
    qint64 totalWords = -1;
    int nativeEntries = -1, nativeCounts = -1;
    QString error;
    bool ready = false;
};

// Optional platform chapter work. The owner cancels it on destruction and
// drains its worker before releasing publication access or exporting storage.
class ReadingChapters {
public:
    virtual ~ReadingChapters() = default;
    virtual const ReadingChapterData &data() const = 0;
    std::function<void()> updated;
};

// GUI-thread services for a platform's existing reading database and activity
// recorder. A standalone reader has no bridge; local state still belongs to
// ReadingBook. The bridge never owns the engine session or publication access.
class ReadingBridge {
public:
    virtual ~ReadingBridge() = default;
    virtual bool deviceTransition() const = 0;
    virtual bool writable() const = 0;
    // Sleep may still update the saved position. USB and other device
    // transitions stay blocked. Ordinary reading activity uses writable().
    virtual bool allowsSleepSnapshot() const {
        return false;
    }
    virtual ReadingProgress read(QString *error = nullptr) const = 0;
    virtual bool takeChange() = 0;
    virtual bool write(const ReadingProgress &expected, const QString &location, double percent, bool finished,
                       ReadingProgress *verified, QString *error) = 0;
    virtual void observeReading(double percent, const QString &position) = 0;
    virtual void stopReading() = 0;
    virtual ReadingChapters *chapters() = 0;
};
}
