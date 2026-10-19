#include "platform/nickel/common/usb_transition.h"

#include <QPointer>
#include <QTest>

#include <memory>
#include <utility>

namespace {
class DelayedReader : public kobalt::ReaderWindow {
public:
    // Like ReaderSession::nativePosition, completion is discarded if the
    // reader dies while its position conversion is still on the worker.
    std::function<void()> converted;
    void prepareUsb(std::function<void()> finished) override {
        QPointer<DelayedReader> guard(this);
        converted = [guard, finished = std::move(finished)] {
            if (guard) {
                finished();
            }
        };
    }
    bool readyForPresentation() const override {
        return true;
    }
    void checkpoint() override {}
    void prepareShutdown(std::function<void()>) override {}
    void sleepReader() override {}
    void prepareWake() override {}
    void cancelWake() override {}
    void sleepViewReady() override {}
    void wakeReader() override {}
    void progressTick() override {}
    void refreshHostPresentation() override {}
    void pageButton(int) override {}
};
}

class UsbTransitionTest : public QObject {
    Q_OBJECT
private slots:
    void completionSurvivesInterruptedSave_data() {
        QTest::addColumn<bool>("destroyReader");
        QTest::newRow("completed-save") << false;
        QTest::newRow("revocation-during-position-conversion") << true;
    }
    void completionSurvivesInterruptedSave() {
        QFETCH(bool, destroyReader);
        auto reader = std::make_unique<DelayedReader>();
        QVector<std::function<void()>> cleanupCallbacks;
        QStringList events;
        int drains = 0;
        const auto release = [&] {
            ++drains;
            reader.reset();
        };
        kobalt::nickel::prepareUsbTransition(reader.get(), cleanupCallbacks, release, [&] {
            events.append("invalidate-assets");
            events.append("usb-ready");
        });
        auto converted = reader->converted;
        QVERIFY(events.isEmpty());
        QCOMPARE(drains, 0);
        if (destroyReader) {
            // Authorization revocation disposes the window and starts the
            // same drain without waiting for its position conversion.
            release();
        }
        converted();
        QCOMPARE(drains, 1);
        QVERIFY(events.isEmpty());
        events.append("engine-drained");
        events.append("resources-released");
        // ReaderCoordinator consumes this queue only after both barriers.
        const auto finishDrain = [&] {
            const auto callbacks = std::exchange(cleanupCallbacks, {});
            for (const auto &callback : callbacks) {
                callback();
            }
        };
        finishDrain();
        converted();
        finishDrain();
        QCOMPARE(events, QStringList({"engine-drained", "resources-released", "invalidate-assets", "usb-ready"}));
        QCOMPARE(drains, 1);
    }
    void noReaderStillWaitsForDrain() {
        QVector<std::function<void()>> cleanupCallbacks;
        int drains = 0, completions = 0;
        kobalt::nickel::prepareUsbTransition(
            nullptr, cleanupCallbacks,
            [&] {
                ++drains;
            },
            [&] {
                ++completions;
            });
        QCOMPARE(drains, 1);
        QCOMPARE(completions, 0);
        QCOMPARE(cleanupCallbacks.size(), 1);
        cleanupCallbacks.front()();
        QCOMPARE(completions, 1);
    }
};

QTEST_MAIN(UsbTransitionTest)
#include "usb_transition_test.moc"
