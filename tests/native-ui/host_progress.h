#pragma once
#include "reading/reading_bridge.h"
#include <memory>

namespace kobalt::test {
// The platform's saved reading position, which a test changes as a sync or
// another device would. Writes from the reader are recorded, not stored.
struct HostProgress {
    ReadingProgress current;
    bool changed = false, failWrite = false;
    int writes = 0;
    double percent = 0;
    void publish(ReadingProgress progress) {
        current = std::move(progress);
        changed = true;
    }
};
std::unique_ptr<ReadingBridge> hostReadingBridge(std::shared_ptr<HostProgress> progress);
}
