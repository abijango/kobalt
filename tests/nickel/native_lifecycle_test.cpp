#include "platform/nickel/common/native_lifecycle.h"
#include "engine/crengine/engine_activity.h"
#include <QCoreApplication>
#include <QTest>

// These host objects expose the symbol names and Qt signals used by the
// adapter. They test Kobalt's lifecycle decisions, not Nickel's private ABI.
class N3PowerWorkflowManager : public QObject {
    Q_OBJECT
public:
    static N3PowerWorkflowManager *sharedInstance();
    bool sleeping() const;
    bool sleepViewVisible() const;
    bool pinCodeInputDialogVisible() const;
    bool asleep = false, sleepVisible = false, pinVisible = false;
signals:
    void aboutToSleep();
    void resumedFromSleep();
};

class PlugWorkflowManager : public QObject {
    Q_OBJECT
public:
    static PlugWorkflowManager *sharedInstance();
    bool pluggedInViewVisible();
    bool visible = false;
signals:
    void doneProcessing();
    void aboutToUnplug();
};

N3PowerWorkflowManager *N3PowerWorkflowManager::sharedInstance() {
    static N3PowerWorkflowManager manager;
    return &manager;
}
bool N3PowerWorkflowManager::sleeping() const {
    return asleep;
}
bool N3PowerWorkflowManager::sleepViewVisible() const {
    return sleepVisible;
}
bool N3PowerWorkflowManager::pinCodeInputDialogVisible() const {
    return pinVisible;
}
PlugWorkflowManager *PlugWorkflowManager::sharedInstance() {
    static PlugWorkflowManager manager;
    return &manager;
}
bool PlugWorkflowManager::pluggedInViewVisible() {
    return visible;
}

extern "C" void kobalt_hide_sleep_view(void *);
extern "C" void kobalt_show_sleep_view(void *);
extern "C" void kobalt_reboot(void *);
extern "C" void kobalt_power_off(void *, bool);
extern "C" bool kobalt_can_suspend(void *);

namespace {
int reboots = 0, powerOffs = 0;
bool lastBatteryLow = false, reenterReboot = false, acceptWake = true, nativeSuspendAllowed = true;
void reboot(void *manager) {
    ++reboots;
    if (reenterReboot)
        kobalt_reboot(manager);
}
void powerOff(void *, bool batteryLow) {
    ++powerOffs;
    lastBatteryLow = batteryLow;
}
void hideSleep(void *) {
    if (acceptWake)
        N3PowerWorkflowManager::sharedInstance()->sleepVisible = false;
}
void showSleep(void *) {
    N3PowerWorkflowManager::sharedInstance()->sleepVisible = true;
}
void noOp(void *) {}
bool canSuspend(void *) {
    return nativeSuspendAllowed;
}
}

class NativeLifecycleTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        auto *power = N3PowerWorkflowManager::sharedInstance();
        power->asleep = power->sleepVisible = power->pinVisible = false;
        PlugWorkflowManager::sharedInstance()->visible = false;
        reboots = powerOffs = 0;
        lastBatteryLow = reenterReboot = false;
        acceptWake = nativeSuspendAllowed = true;
        kobalt::engineActivity().pause(false);
        kobalt::nativeShowSleepView = showSleep;
        kobalt::nativeHideSleepView = hideSleep;
        kobalt::nativeCanSuspend = canSuspend;
        kobalt::nativeUsbConnect = noOp;
        kobalt::nativeAboutUsbConnect = noOp;
        kobalt::nativePowerOff = powerOff;
        kobalt::nativeReboot = reboot;
    }

    void cleanup() {
        kobalt::engineActivity().pause(false);
    }

    void missingTransitionHookLeavesLifecycleDisconnected_data() {
        QTest::addColumn<int>("hook");
        QTest::newRow("show-sleep") << 0;
        QTest::newRow("hide-sleep") << 1;
        QTest::newRow("can-suspend") << 2;
        QTest::newRow("usb-connect") << 3;
        QTest::newRow("about-usb-connect") << 4;
        QTest::newRow("power-off") << 5;
        QTest::newRow("reboot") << 6;
    }

    void missingTransitionHookLeavesLifecycleDisconnected() {
        QFETCH(int, hook);
        switch (hook) {
        case 0:
            kobalt::nativeShowSleepView = nullptr;
            break;
        case 1:
            kobalt::nativeHideSleepView = nullptr;
            break;
        case 2:
            kobalt::nativeCanSuspend = nullptr;
            break;
        case 3:
            kobalt::nativeUsbConnect = nullptr;
            break;
        case 4:
            kobalt::nativeAboutUsbConnect = nullptr;
            break;
        case 5:
            kobalt::nativePowerOff = nullptr;
            break;
        case 6:
            kobalt::nativeReboot = nullptr;
            break;
        }
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(!lifecycle.connected());
    }

    void sleepAndPinPreventPrematureResume() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        auto *power = N3PowerWorkflowManager::sharedInstance();
        int sleeps = 0, wakes = 0;
        lifecycle.sleep = [&] {
            ++sleeps;
        };
        lifecycle.wake = [&] {
            ++wakes;
        };
        power->asleep = true;
        emit power->aboutToSleep();
        QCOMPARE(sleeps, 1);
        QVERIFY(lifecycle.blocked());
        power->asleep = false;
        power->pinVisible = true;
        emit power->resumedFromSleep();
        QCoreApplication::processEvents();
        QCOMPARE(wakes, 0);
        QVERIFY(lifecycle.blocked());
        QVERIFY(kobalt::NativeLifecycle::suspended());
        power->pinVisible = false;
        emit power->resumedFromSleep();
        QTRY_COMPARE(wakes, 1);
        QVERIFY(!lifecycle.blocked());
        QVERIFY(!kobalt::NativeLifecycle::suspended());
    }

    void refusedSleepViewRemovalCancelsPreparedWake() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        auto *power = N3PowerWorkflowManager::sharedInstance();
        emit power->aboutToSleep();
        power->sleepVisible = true;
        int prepared = 0, cancelled = 0;
        lifecycle.beforeWake = [&] {
            ++prepared;
        };
        lifecycle.cancelWake = [&] {
            ++cancelled;
        };
        acceptWake = false;
        kobalt_hide_sleep_view(power);
        QCOMPARE(prepared, 1);
        QCOMPARE(cancelled, 1);
        acceptWake = true;
        kobalt_hide_sleep_view(power);
        QCOMPARE(prepared, 2);
        QCOMPARE(cancelled, 1);
        power->pinVisible = true;
        kobalt_hide_sleep_view(power);
        QCOMPARE(prepared, 3);
        QCOMPARE(cancelled, 2);
    }

    void drainingDoesNotPrepareWakeAndSleepCoverWaitsForNativeView() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        auto *power = N3PowerWorkflowManager::sharedInstance();
        int prepared = 0, covered = 0;
        lifecycle.beforeWake = [&] {
            ++prepared;
        };
        lifecycle.sleepViewReady = [&] {
            QVERIFY(power->sleepVisible);
            ++covered;
        };
        kobalt::NativeLifecycle::finishSleepView();
        QCOMPARE(covered, 0);
        emit power->aboutToSleep();
        kobalt_show_sleep_view(power);
        QCOMPARE(covered, 1);
        lifecycle.draining = true;
        kobalt_hide_sleep_view(power);
        QCOMPARE(prepared, 0);
    }

    void preparingWakeCanDestroyLifecycleBeforeNativeHideReturns() {
        auto *lifecycle = new kobalt::NativeLifecycle(nullptr);
        QPointer<kobalt::NativeLifecycle> guard(lifecycle);
        QVERIFY(lifecycle->connected());
        auto *power = N3PowerWorkflowManager::sharedInstance();
        emit power->aboutToSleep();
        lifecycle->beforeWake = [lifecycle] {
            delete lifecycle;
        };
        kobalt_hide_sleep_view(power);
        QVERIFY(guard.isNull());
        QVERIFY(kobalt::NativeLifecycle::canSuspend());
    }

    void suspendWaitsForActiveEngineWorkAndDraining() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        QVERIFY(kobalt_can_suspend(nullptr));
        {
            kobalt::EngineJob job;
            QVERIFY(!kobalt_can_suspend(nullptr));
        }
        QVERIFY(kobalt_can_suspend(nullptr));
        lifecycle.draining = true;
        QVERIFY(!kobalt_can_suspend(nullptr));
        lifecycle.draining = false;
        kobalt::NativeLifecycle::deferSuspend(true);
        QVERIFY(!kobalt_can_suspend(nullptr));
        kobalt::NativeLifecycle::deferSuspend(false);
        QVERIFY(kobalt_can_suspend(nullptr));
        nativeSuspendAllowed = false;
        QVERIFY(!kobalt_can_suspend(nullptr));
    }

    void shutdownCompletionIsConsumedBeforeReentry() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        auto *power = N3PowerWorkflowManager::sharedInstance();
        std::function<void()> complete;
        int preparations = 0;
        lifecycle.prepareShutdown = [&](std::function<void()> ready) {
            ++preparations;
            complete = std::move(ready);
        };
        kobalt_reboot(power);
        QCOMPARE(preparations, 1);
        QCOMPARE(reboots, 0);
        QVERIFY(lifecycle.shuttingDown());
        QVERIFY(!kobalt_can_suspend(power));
        kobalt_reboot(power);
        QCOMPARE(preparations, 1);
        auto copied = complete;
        reenterReboot = true;
        complete();
        copied();
        QCOMPARE(reboots, 1);
        QCOMPARE(preparations, 1);
    }

    void powerOffPreservesBatteryLowReason_data() {
        QTest::addColumn<bool>("batteryLow");
        QTest::newRow("normal") << false;
        QTest::newRow("battery-low") << true;
    }

    void powerOffPreservesBatteryLowReason() {
        QFETCH(bool, batteryLow);
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        std::function<void()> complete;
        lifecycle.prepareShutdown = [&](std::function<void()> ready) {
            complete = std::move(ready);
        };
        kobalt_power_off(N3PowerWorkflowManager::sharedInstance(), batteryLow);
        QCOMPARE(powerOffs, 0);
        complete();
        QCOMPARE(powerOffs, 1);
        QCOMPARE(lastBatteryLow, batteryLow);
        complete();
        QCOMPARE(powerOffs, 1);
    }

    void destroyedShutdownManagerDoesNotReceiveCompletion() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        std::function<void()> complete;
        lifecycle.prepareShutdown = [&](std::function<void()> ready) {
            complete = std::move(ready);
        };
        auto *manager = new QObject;
        QVERIFY(kobalt::NativeLifecycle::interceptShutdown(manager, kobalt::NativeLifecycle::Shutdown::Reboot));
        delete manager;
        complete();
        QCOMPARE(reboots, 0);
    }

    void shutdownWithoutPreparationKeepsNativeBehavior() {
        kobalt::NativeLifecycle lifecycle(nullptr);
        QVERIFY(lifecycle.connected());
        kobalt_power_off(N3PowerWorkflowManager::sharedInstance(), true);
        QCOMPARE(powerOffs, 1);
        QVERIFY(lastBatteryLow);
        QVERIFY(!lifecycle.shuttingDown());
    }
};

QTEST_GUILESS_MAIN(NativeLifecycleTest)
#include "native_lifecycle_test.moc"
