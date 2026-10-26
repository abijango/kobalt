#include "platform/api/kobo_refresh.h"
#include <cerrno>
namespace kobalt::platform {
// iOS has no e-paper waveform. Widget updates go through the normal backing store.
void textRefresh(QWidget *) {}
void pageRefresh(QWidget *, bool, bool) {}
void setReaderRefreshWindow(QWidget *) {}
}
namespace kobalt::nickel4 {
// iOS provides loopback. Never attempt Kobo's privileged interface repair.
bool configureLoopback(int *error) {
    if (error)
        *error = ENOTSUP;
    return false;
}
}
