#include "platforms/android/canvas.h"
#include "platforms/android/reader.h"
#include "services.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "reading/reading_density.h"
#include "support/diagnostics.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

namespace {
int devicePpi(const QScreen *screen) {
    if (!screen)
        return 264;
    const int physical = qRound(screen->physicalDotsPerInch());
    // UIKit often reports the point density here. Prefer a framebuffer density.
    if (physical >= 200 && physical <= 600)
        return physical;
    const int scaled = qRound(screen->logicalDotsPerInch() * screen->devicePixelRatio());
    if (scaled >= 200 && scaled <= 600)
        return scaled;
    return 264;
}

class Window final : public QWidget {
public:
    Window(kobalt::EngineFontInputs fonts, QString placeholder, QString book)
        : book(std::move(book)),
          controller(kobalt::storage::Paths{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                            "/config"},
                     std::move(fonts), std::move(placeholder), kobalt::ios::readerServices,
                     [](Qt::ScreenOrientation) { /* iOS owns display rotation. */ }) {
        setWindowTitle("Kobalt");
        auto *layout = new QStackedLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        message = new QLabel(QStringLiteral("Opening…"));
        message->setAlignment(Qt::AlignCenter);
        message->setWordWrap(true);
        canvas = new kobalt::android::WidgetCanvas(this);
        canvas->viewportResized = [this] {
            canvas->fitContent();
        };
        layout->addWidget(message);
        layout->addWidget(canvas);
        connect(&controller, &kobalt::android::ReaderController::openingProgress, this, [this](int percent) {
            message->setText(QStringLiteral("Opening… %1%").arg(percent));
        });
        connect(&controller, &kobalt::android::ReaderController::opened, this, [this] {
            auto *reader = controller.window();
            canvas->setContent(reader);
            if (auto *refresh = reader->findChild<QWidget *>("rrScreenRefresh"))
                refresh->hide();
            reader->show();
            canvas->fitContent();
            static_cast<QStackedLayout *>(this->layout())->setCurrentWidget(canvas);
        });
        connect(&controller, &kobalt::android::ReaderController::failed, this, [this](const QString &error) {
            failed = true;
            message->setText(error);
            static_cast<QStackedLayout *>(this->layout())->setCurrentWidget(message);
        });
        connect(&controller, &kobalt::android::ReaderController::closed, this, [this] {
            if (!failed && controller.idle())
                QCoreApplication::quit();
        });
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive)
                if (auto *reader = controller.window())
                    reader->checkpoint();
        });
    }

protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        QTimer::singleShot(0, this, [this] {
            openSample();
        });
    }
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        openSample();
    }

private:
    QString book;
    kobalt::android::ReaderController controller;
    kobalt::android::WidgetCanvas *canvas = nullptr;
    QLabel *message = nullptr;
    bool started = false, failed = false;
    void openSample() {
        const auto pixels = canvas->pixelSize();
        if (started || pixels.isEmpty())
            return;
        if (!QFileInfo::exists(book)) {
            started = true;
            failed = true;
            message->setText(QStringLiteral("The bundled sample book is missing:\n%1").arg(book));
            return;
        }
        started = true;
        controller.open(book, pixels);
    }
};
}
int main(int argc, char **argv) {
    using namespace kobalt;
    FontCopies copies;
    QApplication app(argc, argv);
    app.setApplicationName("Kobalt");
    app.setOrganizationName("Kobalt");
    app.setQuitOnLastWindowClosed(false);
    const int ppi = devicePpi(app.primaryScreen());
    readingPpi.store(ppi);
    ui::setDensity(ppi);
    const auto assetsRoot = QDir(QCoreApplication::applicationDirPath()).filePath("assets");
    AssetBundle assets(assetsRoot, QStringLiteral(KOBALT_VERSION));
    if (!assets.available() || !ui::initializeInterfaceFonts(assets)) {
        qCritical() << "Kobalt bundled assets are unavailable at" << assetsRoot;
        return 2;
    }
    for (const auto &file : bundledFontFiles(assets))
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qCritical() << "Cannot register font" << file;
            return 2;
        }
    app.setFont(ui::interfaceFont());
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    initializeDiagnostics(root + "/diagnostics");
    const auto book = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(KOBALT_IOS_SAMPLE_BOOK));
    Window window(engineFontInputs(assets, {}, {}, {}), placeholderFontFamily(assets, copies), book);
    window.showFullScreen();
    qInfo() << "Kobalt iOS ready; density" << ppi;
    return app.exec();
}
