#pragma once
#include "platform/api/reader_orientation.h"
#include "reading/reader_session.h"
#include "interfaces/reader_window.h"
#include "storage/reader_storage.h"
#include "publication/publication_endpoint.h"
#include "host_progress.h"
#include "platform/api/reader_host_services.h"
#include <QPointer>

namespace kobalt::test {
// Test host composition. Publication access and the engine are production
// implementations; only services normally provided by Nickel are substitutes.
class ReaderHost : public QObject {
    Q_OBJECT
public:
    ReaderHost(storage::Paths paths, EngineFontInputs fonts, QString placeholderFamily, QObject *parent = nullptr);
    ~ReaderHost() override;
    void open(const QString &path, QSize nativeSize = {1072, 1448});
    void shutdown();
    ReaderWindow *window() const {
        return window_;
    }
    ReaderSession *session() const {
        return session_;
    }
    bool idle() const {
        return phase == Phase::Idle;
    }
    // Optional platform services for the next open(). Without them the book
    // has no saved platform position and no position mapping.
    std::shared_ptr<HostProgress> progress;
    PositionCodec positions;
    std::function<void(ReaderHostServices &)> configureServices;
signals:
    void opened();
    void failed(QString error);
    void closed();

private:
    enum class Phase { Idle, Opening, Reading, Closing };
    Phase phase = Phase::Idle;
    storage::Paths paths;
    EngineFontInputs fonts;
    QString placeholder;
    std::unique_ptr<QOrientationSensor> fixtureSensor;
    ReaderOrientation orientation;
    std::unique_ptr<native::PublicationEndpoint> source;
    std::unique_ptr<ReaderSession> opening;
    QPointer<ReaderWindow> window_;
    QPointer<ReaderSession> session_;
    std::shared_ptr<SourceLease> lease;
    void retire();
};
}
