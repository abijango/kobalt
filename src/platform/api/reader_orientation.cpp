#include "support/qt_compat.h"
#include "reader_orientation.h"
#include <QScreen>

namespace kobalt {
namespace {
Qt::ScreenOrientation validOrientation(int value) {
    switch (value) {
    case Qt::PortraitOrientation:
    case Qt::LandscapeOrientation:
    case Qt::InvertedPortraitOrientation:
    case Qt::InvertedLandscapeOrientation:
        return static_cast<Qt::ScreenOrientation>(value);
    default:
        return Qt::PortraitOrientation;
    }
}
}

Qt::ScreenOrientation shownOrientation(const QScreen *screen) {
    if (!screen)
        return Qt::PrimaryOrientation;
    const auto orientation = screen->orientation();
    return orientation == Qt::PrimaryOrientation ? screen->primaryOrientation() : orientation;
}

ReaderOrientation::ReaderOrientation(QOrientationSensor *sensor, std::function<void(Qt::ScreenOrientation)> turn,
                                     std::function<void(bool)> rotationOwnership)
    : rotationOwnership(std::move(rotationOwnership)), sensor(sensor), turn(std::move(turn)) {
    debounce.setSingleShot(true);
    debounce.setInterval(250);
    connect(&debounce, &QTimer::timeout, this, [this] {
        deliver();
    });
}
ReaderOrientation::~ReaderOrientation() {
    stop();
}
void ReaderOrientation::configure(const QJsonObject &settings) {
    manualOrientation = validOrientation(settings.value("orientation").toInt(Qt::PortraitOrientation));
    if (sensor) {
        manualOrientation =
            manualOrientation == Qt::LandscapeOrientation || manualOrientation == Qt::InvertedLandscapeOrientation
                ? Qt::LandscapeOrientation
                : Qt::PortraitOrientation;
    }
    const bool automatic = settings.value("auto_rotate").toBool(false);
    if (active && (!sensor || (!automatic && currentOrientation != manualOrientation &&
                               currentOrientation != Qt::ScreenOrientation(int(manualOrientation) * 4)))) {
        apply(manualOrientation);
    }
    setAutomatic(automatic);
}
void ReaderOrientation::start(Qt::ScreenOrientation orientation) {
    if (active)
        return;
    nativeOrientation = orientation;
    active = true;
    if (rotationOwnership) {
        rotationOwnership(true);
    }
    if (sensor)
        sensor->addFilter(this);
    const bool automatic = automaticRotation;
    apply(manualOrientation);
    setAutomatic(automatic);
}
void ReaderOrientation::stop() {
    if (!active)
        return;
    active = false;
    debounce.stop();
    pending = Qt::PrimaryOrientation;
    if (sensor) {
        sensor->removeFilter(this);
        if (startedSensor)
            sensor->stop();
    }
    startedSensor = false;
    // PrimaryOrientation is not the orientation Nickel showed; see shownOrientation().
    if (nativeOrientation != Qt::PrimaryOrientation)
        apply(nativeOrientation);
    if (rotationOwnership) {
        rotationOwnership(false);
    }
}
void ReaderOrientation::rotate() {
    setAutomatic(false);
    manualOrientation = currentOrientation == Qt::InvertedLandscapeOrientation
                            ? Qt::PortraitOrientation
                            : static_cast<Qt::ScreenOrientation>(int(currentOrientation) * 2);
    if (active)
        apply(manualOrientation);
}
bool ReaderOrientation::setAutomatic(bool enabled) {
    debounce.stop();
    pending = Qt::PrimaryOrientation;
    automaticRotation = enabled && sensor;
    if (active && sensor) {
        if (!sensor->isActive()) {
            startedSensor = sensor->start();
            if (!startedSensor) {
                automaticRotation = false;
                return false;
            }
        }
        if (sensor->reading())
            filter(sensor->reading());
    }
    return !enabled || automaticRotation;
}
void ReaderOrientation::setBlocked(bool value) {
    blocked = value;
    if (!blocked && pending != Qt::PrimaryOrientation && !debounce.isActive())
        debounce.start(0);
}
bool ReaderOrientation::filter(QOrientationReading *reading) {
    if (!active)
        return true;
    if (sensor) {
        Qt::ScreenOrientation next = Qt::PrimaryOrientation;
        switch (reading->orientation()) {
        case QOrientationReading::TopUp:
            next = Qt::PortraitOrientation;
            break;
        case QOrientationReading::TopDown:
            next = Qt::InvertedPortraitOrientation;
            break;
        case QOrientationReading::LeftUp:
            next = Qt::LandscapeOrientation;
            break;
        case QOrientationReading::RightUp:
            next = Qt::InvertedLandscapeOrientation;
            break;
        default:
            break;
        }
        const bool portrait = next == Qt::PortraitOrientation || next == Qt::InvertedPortraitOrientation;
        const bool allowed = automaticRotation || portrait == (manualOrientation == Qt::PortraitOrientation);
        if (next == Qt::PrimaryOrientation || !allowed || next == currentOrientation) {
            pending = Qt::PrimaryOrientation;
            debounce.stop();
        } else if (next != pending) {
            // Repeated samples at the same angle must not postpone the turn.
            pending = next;
            debounce.start(250);
        }
    }
    // Nickel keeps its connection. It receives readings again after stop().
    return false;
}
void ReaderOrientation::deliver() {
    if (!active || !sensor || blocked || pending == Qt::PrimaryOrientation)
        return;
    const auto orientation = pending;
    pending = Qt::PrimaryOrientation;
    apply(orientation);
}
QJsonObject ReaderOrientation::settings() const {
    return kobalt::qt::object({{"orientation", int(manualOrientation)}, {"auto_rotate", automaticRotation}});
}
}
