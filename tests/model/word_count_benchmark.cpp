#include "engine/crengine/reader_model.h"

#include "check.h"
#include "reader_inputs.h"
#include "support/qt_compat.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <sys/resource.h>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 4, "Pass EPUB, cache directory, and count or baseline");
    QFile file(QString::fromLocal8Bit(argv[1]));
    require(file.open(QIODevice::ReadOnly), "Cannot open the benchmark EPUB");
    const auto bytes = file.readAll();
    const auto fingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const bool counting = QByteArray(argv[3]) == "count";
    require(counting || QByteArray(argv[3]) == "baseline", "Unknown benchmark mode");
    kobalt::ReaderModel model(kobalt::test::readerFonts());
    require(model.initializeFonts(), "Cannot initialize benchmark fonts");
    model.setDocumentCache(QString::fromLocal8Bit(argv[2]), fingerprint, "word-count-benchmark", bytes.size());
    QString error;
    QElapsedTimer elapsed;
    elapsed.start();
    require(model.open(bytes, QSize(1072, 1328), {}, &error), qPrintable(error));
    const double opened = double(elapsed.nsecsElapsed()) / 1000000;
    kobalt::DocumentWordCounts counts;
    elapsed.restart();
    if (counting) {
        counts = model.wordCounts();
        require(counts.total >= 0, "Logical count failed");
    }
    const double countTime = double(elapsed.nsecsElapsed()) / 1000000;
    struct rusage usage {};
    require(getrusage(RUSAGE_SELF, &usage) == 0, "Cannot measure peak RSS");
    const auto json = kobalt::qt::object({{"mode", counting ? "count" : "baseline"},
                                          {"source_bytes", bytes.size()},
                                          {"document_cache_hit", model.openedFromCache()},
                                          {"open_ms", opened},
                                          {"count_ms", countTime},
                                          {"open_with_count_ms", opened + countTime},
                                          {"total_words", double(counts.total)},
                                          {"chapters", counts.chapters.size()},
                                          {"count_cache_hit", counts.cached},
#ifdef __APPLE__
                                          {"peak_rss_bytes", double(usage.ru_maxrss)}});
#else
                                          {"peak_rss_bytes", double(usage.ru_maxrss) * 1024}});
#endif
    require(model.saveDocumentCache(), "Cannot save benchmark cache");
    puts(QJsonDocument(json).toJson(QJsonDocument::Compact).constData());
}
