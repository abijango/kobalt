#include "platform/nickel/common/usb_dialog_overlay.h"

#include <QPointer>
#include <QPushButton>
#include <QTest>

#include <memory>

class UsbDialogOverlayTest : public QObject {
    Q_OBJECT
private slots:
    void cancelRestoresNativeParentAndActions() {
        QWidget native, reader;
        QWidget dialog(&native);
        QPushButton cancel(&dialog);
        native.show();
        reader.show();
        dialog.setGeometry(10, 20, 240, 300);
        dialog.show();
        const auto flags = dialog.windowFlags();
        const auto geometry = dialog.geometry();
        int cancelled = 0;
        connect(&cancel, &QPushButton::clicked, &dialog, [&] {
            ++cancelled;
            dialog.hide();
        });
        kobalt::nickel::UsbDialogOverlay overlay;
        for (int attempt = 0; attempt != 2; ++attempt) {
            dialog.show();
            overlay.present(&dialog, &reader);
            QVERIFY(overlay.active());
            QCOMPARE(dialog.parentWidget(), &reader);
            QVERIFY(dialog.isVisible());
            cancel.click();
            QVERIFY(!overlay.active());
            QCOMPARE(dialog.parentWidget(), &native);
            QCOMPARE(dialog.windowFlags(), flags);
            QCOMPARE(dialog.geometry(), geometry);
            QVERIFY(!dialog.isVisible());
            QVERIFY(reader.isVisible());
        }
        QCOMPARE(cancelled, 2);
    }

    void readerDestructionDoesNotDestroyNativeDialog() {
        QWidget native;
        auto reader = std::make_unique<QWidget>();
        QPointer<QWidget> dialog = new QWidget(&native);
        native.show();
        reader->show();
        dialog->show();
        kobalt::nickel::UsbDialogOverlay overlay;
        overlay.present(dialog, reader.get());
        reader.reset();
        QVERIFY(dialog);
        QVERIFY(!overlay.active());
        QCOMPARE(dialog->parentWidget(), &native);
    }

    void dialogDestructionDoesNotLeaveActiveOverlay() {
        QWidget native, reader;
        auto *dialog = new QWidget(&native);
        native.show();
        reader.show();
        dialog->show();
        kobalt::nickel::UsbDialogOverlay overlay;
        overlay.present(dialog, &reader);
        delete dialog;
        QVERIFY(!overlay.active());
        overlay.restore();
    }

    void overlayDestructionRestoresNativeOwnership() {
        QWidget native, reader;
        QWidget dialog(&native);
        native.show();
        reader.show();
        dialog.show();
        {
            kobalt::nickel::UsbDialogOverlay overlay;
            overlay.present(&dialog, &reader);
        }
        QCOMPARE(dialog.parentWidget(), &native);
        QVERIFY(!dialog.isVisible());
    }
};

QTEST_MAIN(UsbDialogOverlayTest)
#include "usb_dialog_overlay_test.moc"
