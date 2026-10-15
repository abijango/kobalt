#pragma once
#include <QImage>
#include <QRegion>
#include <cstring>

namespace kobalt {
class KeyboardRefreshCache {
public:
    void clear() {
        pixels = {};
        bounds = {};
        submitted = {};
        lastPolicy = 0;
    }
    bool unchanged(const QImage *image, const QRegion &damage, const QRect &keyboard, quint64 policy, bool maySkip) {
        if (!image || image->depth() != 32 || keyboard.isEmpty() || !image->rect().contains(keyboard)) {
            clear();
            return false;
        }
        const QRegion inside = damage.intersected(keyboard).translated(-keyboard.topLeft());
        if (bounds != keyboard || pixels.format() != image->format()) {
            bounds = keyboard;
            pixels = image->copy(keyboard);
            lastPolicy = policy;
            submitted = inside;
            return false;
        }
        if (policy != lastPolicy) {
            submitted = {};
        }
        const bool covered = inside.subtracted(submitted).isEmpty();
        bool same = true;
        for (const auto &part :
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
             inside.rects()
#else
             inside
#endif
        ) {
            const int bytes = part.width() * 4;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                const auto *source = image->constScanLine(y + keyboard.top()) + (part.left() + keyboard.left()) * 4;
                auto *previous = pixels.scanLine(y) + part.left() * 4;
                if (std::memcmp(source, previous, bytes) != 0) {
                    same = false;
                    std::memcpy(previous, source, bytes);
                }
            }
        }
        const bool skip = maySkip && damage.boundingRect().isValid() && keyboard.contains(damage.boundingRect()) &&
                          policy == lastPolicy && covered && same;
        submitted += inside;
        lastPolicy = policy;
        return skip;
    }

private:
    QImage pixels;
    QRect bounds;
    QRegion submitted;
    quint64 lastPolicy = 0;
};
}
