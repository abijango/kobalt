#include "platform/nickel/common/native_light.h"
#include "platform/nickel/common/publication_source.h"

#include <QApplication>
#include <QTest>
#include <QTimer>

// These objects expose the firmware 4 symbols used by the real adapter.
// The timer represents a native brightness animation still writing its target.
namespace {
int level = 50, saved = 50, cancellations = 0;
QTimer *fade = nullptr;
}
class Device {
public:
    static const Device *getCurrentDevice();
    bool supportsLight() const;
};
const Device *Device::getCurrentDevice() {
    static Device device;
    return &device;
}
bool Device::supportsLight() const {
    return true;
}
class FrontLight : public QObject {
public:
    explicit FrontLight(QObject *parent);
    int brightness() const;
    int maxBrightness() const;
    void setBrightness(int value, int duration) const;
    void cancelBrightnessChanging();
};
FrontLight::FrontLight(QObject *parent) : QObject(parent) {}
int FrontLight::brightness() const {
    return level;
}
int FrontLight::maxBrightness() const {
    return 100;
}
void FrontLight::setBrightness(int value, int) const {
    level = value;
}
void FrontLight::cancelBrightnessChanging() {
    ++cancellations;
    fade->stop();
}
class BrightnessEventDelegate;
class BrightnessEventFilter : public QObject {
public:
    BrightnessEventFilter(QObject *parent, BrightnessEventDelegate *);
    void finishGesture();
};
BrightnessEventFilter::BrightnessEventFilter(QObject *parent, BrightnessEventDelegate *) : QObject(parent) {}
void BrightnessEventFilter::finishGesture() {
    saved = level;
}
namespace kobalt::native {
const PublicationSource &publicationSource() {
    static const PublicationSource source{[] {
                                              return true;
                                          },
                                          nullptr};
    return source;
}
}
class NativeLightTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        level = saved = 50;
        cancellations = 0;
        fade = new QTimer(this);
        fade->setSingleShot(true);
        connect(fade, &QTimer::timeout, this, [] {
            level = 5;
        });
    }
    void cleanup() {
        delete fade;
        fade = nullptr;
    }
    void manualAdjustmentStopsNativeFade() {
        QWidget anchor;
        kobalt::NativeLight light(&anchor);
        QVERIFY(light.initialize());
        fade->start(20);
        QVERIFY(light.beginGesture());
        QCOMPARE(light.setGestureLevel(70), 70);
        light.finishGesture(true);
        QTest::qWait(60);
        QCOMPARE(level, 70);
        QCOMPARE(saved, 70);
        QCOMPARE(cancellations, 1);
    }
    void unchangedManualLevelStopsNativeFade() {
        QWidget anchor;
        kobalt::NativeLight light(&anchor);
        fade->start(20);
        QVERIFY(light.beginGesture());
        QCOMPARE(light.setGestureLevel(50), 50);
        light.finishGesture(true);
        QTest::qWait(60);
        QCOMPARE(level, 50);
        QCOMPARE(saved, 50);
    }
    void cancellationRestoresOriginalLevel() {
        QWidget anchor;
        kobalt::NativeLight light(&anchor);
        fade->start(20);
        QVERIFY(light.beginGesture());
        QCOMPARE(light.setGestureLevel(70), 70);
        light.finishGesture(false);
        QTest::qWait(60);
        QCOMPARE(level, 50);
        QCOMPARE(saved, 50);
    }
    void observingLightDoesNotStopNativeFade() {
        QWidget anchor;
        kobalt::NativeLight light(&anchor);
        QVERIFY(light.initialize());
        fade->start(20);
        QCOMPARE(light.brightness(), 50);
        QCOMPARE(light.setGestureLevel(70), -1);
        QTest::qWait(60);
        QCOMPARE(light.brightness(), 5);
        QCOMPARE(cancellations, 0);
    }
};
QTEST_MAIN(NativeLightTest)
#include "native_light_test.moc"
