#include "check.h"
#include "publication/local_resources.h"
#include "reader_inputs.h"
#include "reading/reader_session.h"
#include "support/diagnostics.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTimer>

using namespace kobalt;
namespace {
QByteArray pixels(const QImage &image) {
    return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()),
                                    QCryptographicHash::Sha256);
}
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 3, "Expected local-cache and illustrated EPUB fixtures");
    QTemporaryDir directory;
    require(directory.isValid(), "temporary cache directory");
    initializeDiagnostics(directory.path() + "/diagnostics");
    QTimer::singleShot(0, &app, [&] {
        std::unique_ptr<ReaderSession> session;
        std::unique_ptr<native::PublicationEndpoint> source;
        const auto cache = directory.path() + "/documents";
        auto release = [&] {
            auto retained = session->retained();
            QEventLoop drained;
            ReaderSession::drain([&] {
                session.reset();
                retained.reset();
                ReaderSession::drain([&] {
                    source.reset();
                    drained.quit();
                });
            });
            drained.exec();
        };
        auto open = [&](const QString &path, bool cancelled = false) {
            source = native::openLocalPublication(path);
            require(source && source->port() == 0, "Local reader requires a socket");
            auto lease = std::make_shared<SourceLease>();
            if (cancelled) {
                lease->cancel();
            }
            session = std::make_unique<ReaderSession>(test::readerFonts(), "Libron");
            OpenRequest request{path,
                                path,
                                QSize(758, 900),
                                {},
                                {},
                                directory.path() + "/layout.json",
                                lease,
                                source->resources(lease)};
            request.documentCachePath = cache;
            QEventLoop opened;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &opened, [] {
                require(false, "Local reader timed out");
            });
            timeout.start(15000);
            bool finished = false;
            OpeningResult outcome;
            session->open(std::move(request), [&](OpeningResult result) {
                finished = true;
                outcome = std::move(result);
                opened.quit();
            });
            opened.exec();
            require(finished, "Local reader did not report its outcome");
            return outcome;
        };
        const auto path = QString::fromLocal8Bit(argv[1]);
        require(open(path).ready() && !session->image().isNull(), "Local EPUB did not render");
        require(!session->resourceStats()["model_cache_hit"].toBool(), "Cold opening reused a document");
        const auto first = pixels(session->image());
        const auto pages = session->state()["pages"].toInt();
        release();
        require(!QDir(cache).entryList({"*.cr3"}, QDir::Files).isEmpty(),
                "Local opening did not save a document cache");
        require(open(path).ready() && session->resourceStats()["model_cache_hit"].toBool(),
                "Local EPUB did not reopen from its document cache");
        require(session->state()["pages"].toInt() == pages && pixels(session->image()) == first,
                "Local cached reopening changed the page");
        require(session->resourceStats()["peak_bytes"].toDouble() <= ResourceCache::budgetBytes,
                "Local opening exceeded the source-cache budget");
        release();
        const auto cancelled = open(path, true);
        require(!cancelled.ready() && cancelled.failure()->kind == OpeningFailureKind::Cancelled &&
                    session->image().isNull(),
                "Cancelled local opening restored cached pages");
        release();
        require(open(QString::fromLocal8Bit(argv[2])).ready() && !session->image().isNull(),
                "Illustrated EPUB did not render through local resources");
        release();
        std::puts("PASS: local EPUB rendering, parsed-cache reopening, source budget, cancellation and close barriers");
        app.quit();
    });
    return app.exec();
}
