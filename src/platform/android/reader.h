#pragma once
#include "platform/api/reader_orientation.h"
#include "platform/api/reader_host_services.h"
#include "reading/reader_session.h"
#include "interfaces/reader_window.h"
#include "storage/reader_storage.h"
#include "publication/publication_endpoint.h"
#include <QPointer>

namespace kobalt::android {
// Own the publication until engine work has drained.
// A newer document replaces a pending opening; closing always cancels it.
class ReaderController : public QObject {
    Q_OBJECT
public:
    ReaderController(storage::Paths paths, EngineFontInputs fonts, QString placeholder,
                     std::function<ReaderHostServices()> services, std::function<void(Qt::ScreenOrientation)> rotate,
                     QObject *parent = nullptr);
    ~ReaderController() override;
    void open(const QString &path, QSize pixels);
    void close();
    QJsonObject lastBook() const;
    bool forgetLastBook();
    ReaderWindow *window() const {
        return window_;
    }
    bool idle() const {
        return phase == Phase::Idle;
    }
    const QString &path() const {
        return current;
    }
signals:
    void opened();
    void openingProgress(int percent);
    void failed(QString error);
    void closed();
    void exitRequested();

private:
    enum class Phase { Idle, Opening, Reading, Closing };
    Phase phase = Phase::Idle;
    storage::Paths paths;
    EngineFontInputs fonts;
    QString placeholder, current, pending;
    QSize pendingSize;
    std::function<ReaderHostServices()> services;
    ReaderOrientation orientation;
    std::unique_ptr<native::PublicationEndpoint> source;
    std::unique_ptr<ReaderSession> opening;
    QPointer<ReaderWindow> window_;
    std::shared_ptr<SourceLease> lease;
    void begin(const QString &path, QSize pixels);
    void retire();
    void release();
};
}
