#include "platform/api/reader_orientation.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSensorBackend>
#include <QSensorManager>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

using namespace kobalt;
namespace {
class Backend : public QSensorBackend {
public:
    QOrientationReading reading;
    explicit Backend(QSensor *sensor) : QSensorBackend(sensor) {
        setReading(&reading);
    }
    void start() override {}
    void stop() override {}
    void sample(QOrientationReading::Orientation value) {
        reading.setOrientation(value);
        newReadingAvailable();
    }
};
class Factory : public QSensorBackendFactory {
public:
    Backend *backend = nullptr;
    QSensorBackend *createBackend(QSensor *sensor) override {
        return backend = new Backend(sensor);
    }
};
void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
void settle() {
    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Factory factory;
    QSensorManager::registerBackend(QOrientationSensor::sensorType, "kobalt-lock-test", &factory);
    QOrientationSensor sensor;
    sensor.setIdentifier("kobalt-lock-test");
    QList<Qt::ScreenOrientation> turns;
    ReaderOrientation rotation(&sensor, [&](Qt::ScreenOrientation value) {
        turns.append(value);
    });
    rotation.configure({});
    require(!rotation.automatic() && rotation.manual() == Qt::PortraitOrientation, "Default portrait lock");
    rotation.start(Qt::PortraitOrientation);
    require(factory.backend && sensor.isActive(), "Locks start the sensor");
    auto sample = [&](QOrientationReading::Orientation value) {
        factory.backend->sample(value);
        settle();
    };
    sample(QOrientationReading::LeftUp);
    require(turns.size() == 1, "Portrait lock rejects landscape before screen delivery");
    sample(QOrientationReading::TopDown);
    require(turns.last() == Qt::InvertedPortraitOrientation, "Portrait lock permits inverted portrait");
    sample(QOrientationReading::TopDown);
    require(turns.size() == 2, "Repeated readings do not turn again");
    factory.backend->sample(QOrientationReading::TopUp);
    sample(QOrientationReading::FaceUp);
    require(turns.size() == 2, "Flat reading cancels pending turn");
    rotation.configure({{"orientation", int(Qt::InvertedLandscapeOrientation)}});
    require(rotation.manual() == Qt::LandscapeOrientation, "Legacy landscape migration");
    sample(QOrientationReading::RightUp);
    require(turns.last() == Qt::InvertedLandscapeOrientation, "Landscape lock permits inverted landscape");
    const auto count = turns.size();
    sample(QOrientationReading::TopUp);
    require(turns.size() == count, "Landscape lock rejects portrait");
    rotation.configure({{"orientation", int(Qt::PortraitOrientation)}, {"auto_rotate", true}});
    require(turns.size() == count, "Restoring Auto does not force a manual turn");
    rotation.setAutomatic(true);
    sample(QOrientationReading::TopUp);
    require(turns.last() == Qt::PortraitOrientation, "Auto permits portrait");
    rotation.setBlocked(true);
    sample(QOrientationReading::LeftUp);
    require(turns.last() == Qt::PortraitOrientation, "Blocked delivery waits");
    sample(QOrientationReading::FaceDown);
    rotation.setBlocked(false);
    settle();
    require(turns.last() == Qt::PortraitOrientation, "Flat reading clears blocked pending turn");
    rotation.configure({{"orientation", int(Qt::InvertedPortraitOrientation)}, {"auto_rotate", true}});
    require(rotation.automatic() && rotation.manual() == Qt::PortraitOrientation, "Saved Auto migration");
    rotation.stop();
    ReaderOrientation manual(nullptr, [](Qt::ScreenOrientation) {});
    manual.configure({{"orientation", int(Qt::InvertedLandscapeOrientation)}});
    require(manual.manual() == Qt::InvertedLandscapeOrientation, "Sensorless manual preference preserved");
    std::puts("PASS: sensor locks, migration, flat readings, debounce and blocked delivery");
}
