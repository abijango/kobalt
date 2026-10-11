#pragma once
#include "usb_removal.h"
#include <QObject>
#include <QPointer>
#include <functional>

namespace kobalt {
extern void (*nativeShowSleepView)(void *);
extern void (*nativeHideSleepView)(void *);
extern bool (*nativeCanSuspend)(void *);
extern void (*nativeUsbConnect)(void *);
extern void (*nativeAboutUsbConnect)(void *);
extern void (*nativePowerOff)(void *, bool);
extern void (*nativeReboot)(void *);

// Construct only after firmwareSupported() accepts Nickel. Nickel owns power and storage;
// this adapter prepares Kobalt before those native transitions continue.
class NativeLifecycle : public QObject {
    Q_OBJECT
public:
    explicit NativeLifecycle(QObject *parent);
    static bool powerAvailable();
    static bool suspended();
    bool connected() const {
        return connected_;
    }
    bool blocked() const {
        return asleep || usbPending || exported || poweringOff || removalPending;
    }
    bool shuttingDown() const {
        return poweringOff;
    }
    bool storageBlocked() const {
        return usbPending || exported;
    }
    bool draining = false;
    std::function<void()> sleep, sleepViewReady, wake, beforeWake, cancelWake;
    static void finishSleepView();
    // Hold suspend while a sleep-time native progress write is still running.
    // A queued Hardcover update must not use this.
    static void deferSuspend(bool defer);
    std::function<void(std::function<void()>)> prepareUsb;
    std::function<void(std::function<void()>)> prepareShutdown;
    static void disconnected();
    static bool canSuspend();
    static void beginUsb();
    static bool interceptUsb(QObject *manager);
    enum class Shutdown { PowerOff, BatteryLow, Reboot };
    static bool interceptShutdown(QObject *manager, Shutdown mode);
#ifndef KOBALT_READER_HARDWARE
    void testCommand();
    bool testUsb = false;
    int testHandoffs = 0;
    bool testSleepCovered = false, testSleepViewReady = false;
#endif
private slots:
    void aboutToSleep();
    void resumed();
    void processingDone();
    void unplugging();

private:
    bool connected_ = false, asleep = false, usbPending = false, exported = false, poweringOff = false;
    bool suspendDeferred = false;
    bool storageReady = false, remounting = false;
    QPointer<QObject> pendingGadget;
    void finishUsb();
    quint64 generation = 0;
    UsbRemoval usbRemoval;
    bool removalPending = false;
    bool restartAfterRemoval();
};
}
