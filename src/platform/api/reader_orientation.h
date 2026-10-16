#pragma once
#include <QJsonObject>
#include <QOrientationSensor>
#include <QPointer>
#include <QTimer>
#include <functional>

class QScreen;

namespace kobalt {
// The orientation that the screen shows now. Qt 6 reports Qt::PrimaryOrientation
// until the first orientation change, and Nickel makes none at boot. Turning to
// PrimaryOrientation selects the panel's own rotation, which is landscape on the
// Libra Colour. Resolve it from the screen's shape, as Qt 5 does. The result is
// PrimaryOrientation only without a screen.
Qt::ScreenOrientation shownOrientation(const QScreen *screen);

class ReaderOrientation : public QObject, private QOrientationFilter {
public:
    // Ownership begins before the first turn and ends after restoring the native
    // orientation. Repeated start/stop calls do not repeat ownership callbacks.
    ReaderOrientation(QOrientationSensor *sensor, std::function<void(Qt::ScreenOrientation)> turn,
                      std::function<void(bool)> rotationOwnership = {});
    ~ReaderOrientation() override;
    void configure(const QJsonObject &settings);
    // stop() restores nativeOrientation, unless it is PrimaryOrientation.
    void start(Qt::ScreenOrientation nativeOrientation);
    void stop();
    void rotate();
    bool setAutomatic(bool enabled);
    void setBlocked(bool blocked);
    bool automaticAvailable() const {
        return !sensor.isNull();
    }
    // Auto permits every sensor orientation. With Auto off, sensor devices
    // still follow both orientations matching manual(), normalized to an axis.
    // Devices without a sensor keep the exact manual orientation.
    bool automatic() const {
        return automaticRotation;
    }
    Qt::ScreenOrientation manual() const {
        return manualOrientation;
    }
    QJsonObject settings() const;

private:
    std::function<void(bool)> rotationOwnership;
    QPointer<QOrientationSensor> sensor;
    std::function<void(Qt::ScreenOrientation)> turn;
    QTimer debounce;
    Qt::ScreenOrientation manualOrientation = Qt::PortraitOrientation;
    Qt::ScreenOrientation nativeOrientation = Qt::PortraitOrientation;
    Qt::ScreenOrientation currentOrientation = Qt::PortraitOrientation;
    Qt::ScreenOrientation pending = Qt::PrimaryOrientation;
    bool active = false, blocked = false, automaticRotation = false, startedSensor = false;
    bool filter(QOrientationReading *reading) override;
    void deliver();
    void apply(Qt::ScreenOrientation value) {
        currentOrientation = value;
        turn(value);
    }
};
}
