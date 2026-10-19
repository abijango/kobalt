#include "platform/api/kobo_refresh.h"
#include "platform/nickel/display/page_animation.h"
#include "platform/nickel/display/keyboard_refresh.h"
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include "platform/nickel/qt5/refresh.h"
#else
#include "platform/nickel/qt6/refresh.h"
#endif
#include <QApplication>
#include <QBackingStore>
#include <QDialog>
#include <QVariant>
#include <QWindow>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace kobalt::platform;
int calls = 0, animationArms = 0, policyRectangles = 0;
bool animationAvailable = false, collecting = false;
QRegion submittedRegion;
FlushFlags submittedFlags;
void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void capture(QBackingStore *, const QRegion &region, QWindow *, const QPoint &, const FlushFlags &flags) {
    ++calls;
    policyRectangles += flags.size();
    if (collecting) {
        submittedRegion += region;
        submittedFlags.append(flags);
    } else {
        submittedRegion = region;
        submittedFlags = flags;
    }
}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
constexpr int start = 128, partialAttribute = 130, fullAttribute = 131, gc16Attribute = 134, gl16Attribute = 137,
              gck16Attribute = 140, ordinaryAttribute = 166, lightOrdinaryAttribute = 166, imageAttribute = 165;
constexpr quint64 darkFlag = quint64(1) << (164 - start);
#else
constexpr int start = Qt::WA_KoboEpdFlagsStart, partialAttribute = Qt::WA_KoboEpdUpdateModePartial,
              fullAttribute = Qt::WA_KoboEpdUpdateModeFull, gc16Attribute = Qt::WA_KoboEpdWfModeGC16,
              gl16Attribute = Qt::WA_KoboEpdWfModeGL16, gck16Attribute = Qt::WA_KoboEpdWfModeGCK16,
              imageAttribute = Qt::WA_KoboEpdWfModeAutoImage, ordinaryAttribute = Qt::WA_KoboEpdWfModeAutoText,
              lightOrdinaryAttribute = Qt::WA_KoboEpdWfModeAutoText;
constexpr quint64 darkFlag = quint64(1) << (Qt::WA_KoboEpdFlagDarkMode - start);
#endif
constexpr quint64 bit(int attribute) {
    return quint64(1) << (attribute - start);
}
bool attribute(QWidget &widget, int value) {
    return widget.testAttribute(static_cast<Qt::WidgetAttribute>(value));
}
void expectPolicy(quint64 policy, const char *message) {
    require(submittedFlags.size() == 1 && submittedFlags[0].second == policy, message);
}
}

// Count the platform's request without requiring a framebuffer or an ioctl hook.
namespace kobalt {
void armPageAnimation(int, const QRect &, bool, bool) {
    ++animationArms;
}
void cancelPageAnimation() {}
bool pageAnimationsAvailable() {
    return animationAvailable;
}
AnimationPanel pageAnimationPanel() {
    return animationAvailable ? AnimationPanel::Mono : AnimationPanel::None;
}
bool pageAnimationSucceeded() {
    return animationAvailable;
}
PageAnimationResult pageAnimationResult() {
    return PageAnimationResult::None;
}
void beginPageRepair() {}
bool endPageRepair() {
    return false;
}
void installPageAnimationHook() {}
}

int main(int argc, char **argv) {
    kobalt::KeyboardRefreshCache keyboardPixels;
    QImage keyboardImage(120, 100, QImage::Format_RGB32);
    keyboardImage.fill(Qt::white);
    const QRect keyboardArea(10, 30, 100, 60), keyArea(15, 35, 12, 12);
    require(!keyboardPixels.unchanged(&keyboardImage, keyboardImage.rect(), keyboardArea, 1, false),
            "Initial keyboard frame was skipped");
    require(keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 1, true),
            "Unchanged keyboard repaint was not skipped");
    keyboardImage.setPixel(keyArea.topLeft(), qRgb(0, 0, 0));
    require(!keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 1, true), "Changed key was skipped");
    require(keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 1, true),
            "Changed key pixels were not retained");
    require(!keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 2, true),
            "A different keyboard policy was skipped");
    require(!keyboardPixels.unchanged(&keyboardImage, keyboardImage.rect(), keyboardArea, 2, true),
            "Damage outside the keyboard was skipped");
    require(!keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 2, false),
            "Explicit keyboard refresh was skipped");
    keyboardPixels.clear();
    require(!keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 2, true),
            "Keyboard cache survived a screen ownership change");
    const QRect otherKey(40, 35, 12, 12);
    require(!keyboardPixels.unchanged(&keyboardImage, otherKey, keyboardArea, 2, true),
            "An unsubmitted key was skipped after partial initialization");
    require(keyboardPixels.unchanged(&keyboardImage, otherKey, keyboardArea, 2, true),
            "Submitted key coverage was not retained");
    require(!keyboardPixels.unchanged(&keyboardImage, keyArea, keyboardArea, 3, true),
            "A changed policy was skipped on the first key");
    require(!keyboardPixels.unchanged(&keyboardImage, otherKey, keyboardArea, 3, true),
            "A changed policy was skipped on a different key");
    require(!keyboardPixels.unchanged(nullptr, keyArea, keyboardArea, 2, true),
            "Missing backing pixels were treated as unchanged");
    QApplication app(argc, argv);
    using namespace kobalt::platform;
    nativeFlush = capture;
    QDialog reader;
    reader.resize(400, 600);
    reader.setWindowModality(Qt::ApplicationModal);
    QWidget page(&reader);
    page.setGeometry(reader.rect());
    page.show();
    reader.show();
    app.processEvents();
    require(QApplication::activeModalWidget() == &reader, "Reader did not become the active modal window");
    reader.setProperty("reader_refresh_backend", "hwtcon");
    setReaderRefreshWindow(&reader);
    QBackingStore store(reader.windowHandle());
    const QRect first(20, 20, 80, 80), second(160, 20, 80, 80);
    const QRegion split = QRegion(first) | QRegion(second);
    auto flush = [&](const QRegion &damage, const FlushFlags &flags) {
        const int before = calls;
        submittedRegion = {};
        submittedFlags.clear();
        collecting = true;
        flushReader(&store, damage, nullptr, {}, flags);
        collecting = false;
        require(calls > before && calls <= before + qMax(1, damage.rectCount()),
                "Flush submitted more than once per painted region");
    };
    const quint64 darkOrdinary = bit(ordinaryAttribute) | bit(fullAttribute) | darkFlag;
    const quint64 darkClean = bit(imageAttribute) | bit(fullAttribute) | darkFlag;
    reader.setProperty("reader_dark", true);
    reader.setProperty("reader_plain_controls", true);
    pageRefresh(&page, false, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    require(attribute(page, ordinaryAttribute) && attribute(page, fullAttribute) &&
                !attribute(page, partialAttribute) && !attribute(page, gck16Attribute) &&
                !attribute(page, gc16Attribute),
            "Ordinary dark page has the wrong attributes");
    require(attribute(page, Qt::WA_KoboEpdFlagDarkMode), "Dark page is missing its dark flag");
#else
    require(!attribute(page, partialAttribute) && attribute(page, fullAttribute) && !attribute(page, gck16Attribute),
            "Qt5 ordinary page lost its full update mode");
#endif
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkClean, "Entering dark mode did not use image/full");
    require(submittedRegion == QRegion(reader.rect()), "Entering dark mode did not clean the entire screen");
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkOrdinary, "Ordinary dark turn changed its full policy");
    reader.setProperty("reader_progress_update", first);
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkOrdinary, "Progress request changed a dark refresh");
    require(reader.property("reader_progress_update").toRect().isEmpty(), "Dark flush retained a progress request");
    flush(split, {{first, darkOrdinary}, {second, darkOrdinary}});
    require(submittedRegion == split && submittedFlags == FlushFlags({{first, darkOrdinary}, {second, darkOrdinary}}),
            "Separated dark controls refreshed their unchanged gap or changed policy");
    reader.setProperty("reader_page_turn", true);
    flush(split, {{first, darkOrdinary}, {second, darkOrdinary}});
    expectPolicy(bit(ordinaryAttribute) | bit(partialAttribute) | darkFlag,
                 "Ordinary dark page turn did not use partial mode");
    pageRefresh(&page, true, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    require(attribute(page, imageAttribute) && attribute(page, fullAttribute) && !attribute(page, ordinaryAttribute) &&
                !attribute(page, gc16Attribute) && !attribute(page, partialAttribute),
            "Dark cleanup has the wrong attributes");
#else
    require(attribute(page, gck16Attribute) && attribute(page, fullAttribute) && !attribute(page, partialAttribute),
            "Qt5 cleanup lost its widget flags");
#endif
    flush(split, {{first, darkClean}, {second, darkClean}});
    require(submittedRegion == split && submittedFlags.size() == 2,
            "Split dark cleanup was replaced with an ordinary refresh");
    for (const auto &entry : submittedFlags)
        require(entry.second == darkClean, "Split dark cleanup lost image/full");
    reader.setProperty("reader_full_flash", true);
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkClean, "Requested dark cleanup did not use image/full");
    require(!reader.property("reader_full_flash").toBool(), "Cleanup request was not consumed");
    pageRefresh(&page, false, true);
    reader.setProperty("reader_animation_direction", 1);
    flush(first, {{first, bit(gck16Attribute) | bit(partialAttribute) | darkFlag}});
    require(animationArms == 0, "A stray dark direction armed an animation");
    require(reader.property("reader_animation_direction").toInt() == 0, "Animation direction was not consumed");
    reader.setProperty("reader_dark", false);
    pageRefresh(&page, false, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    require(attribute(page, lightOrdinaryAttribute) && attribute(page, fullAttribute) &&
                !attribute(page, partialAttribute) && !attribute(page, gck16Attribute) &&
                !attribute(page, gl16Attribute),
            "Light page did not select native text/full");
    require(!attribute(page, Qt::WA_KoboEpdFlagDarkMode), "Light page retained the dark flag");
#else
    require(!attribute(page, partialAttribute) && attribute(page, fullAttribute),
            "Qt5 ordinary light page lost its full update mode");
#endif
    const quint64 lightText = bit(lightOrdinaryAttribute) | bit(fullAttribute);
    flush(first, {{first, lightText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute), "Light reading bars did not use partial mode");
    flush(split, {{first, lightText}, {second, lightText}});
    const quint64 lightPartial = bit(lightOrdinaryAttribute) | bit(partialAttribute);
    require(submittedRegion == split && submittedFlags == FlushFlags({{first, lightPartial}, {second, lightPartial}}),
            "Separated reading bars refreshed their unchanged gap or changed policy");
    reader.setProperty("reader_controls_opening", true);
    const int beforeControls = calls;
    flush(split, {{first, lightText}, {second, lightText}});
    require(calls == beforeControls + 1 && submittedRegion == QRegion(reader.rect()),
            "Opening both bars did not submit one completed screen");
    expectPolicy(lightPartial, "Opening both bars changed their waveform or partial mode");
    require(!reader.property("reader_controls_opening").toBool(), "Controls opening request was not consumed");
    flush(split, {{first, lightText}, {second, lightText}});
    require(submittedRegion == split, "Controls opening changed a later localized panel update");
    page.hide();
    reader.setProperty("reader_controls_opening", true);
    flush(split, {{first, lightText}, {second, bit(gl16Attribute) | bit(fullAttribute)}});
    require(submittedRegion == split && submittedFlags.size() == 2 &&
                submittedFlags[1].second == (bit(gl16Attribute) | bit(fullAttribute)),
            "Controls opening replaced an explicit mixed waveform");
    reader.setProperty("reader_keyboard_bounds", split.boundingRect());
    const int beforeKeyboard = calls;
    flush(split, {{first, lightText}, {second, lightText}});
    require(calls == beforeKeyboard + 1 && submittedRegion == QRegion(split.boundingRect()),
            "Keyboard key damage was not combined into one rectangle");
    expectPolicy(lightPartial, "Keyboard key damage changed its waveform");
    reader.setProperty("reader_keyboard_bounds", first);
    flush(split, {{first, lightText}, {second, lightText}});
    require(submittedRegion == split, "Keyboard bounds combined unrelated panel damage");
    reader.setProperty("reader_keyboard_bounds", QRect());
    reader.setProperty("reader_plain_controls", false);
    reader.setProperty("reader_progress_update", first);
    flush(first, {{first, lightText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute), "Visible progress bar did not use partial mode");
    require(reader.property("reader_progress_update").toRect().isEmpty(), "Progress update was not consumed");
    flush(first, {{first, lightText}});
    expectPolicy(lightText, "Progress mode leaked into a later refresh");
    reader.setProperty("reader_progress_update", first);
    flush(split, {{first, lightText}, {second, lightText}});
    require(submittedRegion == split && submittedFlags == FlushFlags({{first, lightText}, {second, lightText}}),
            "Progress mode changed damage outside the bar");
    reader.setProperty("reader_progress_update", first);
    flush(first, {{second, lightText}});
    expectPolicy(lightText, "Progress mode ignored an outside source flag rectangle");
    page.hide();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    const quint64 heuristicText = bit(132) | bit(129);
#else
    const quint64 heuristicText = bit(Qt::WA_KoboEpdWfModeAutoText) | bit(Qt::WA_KoboEpdUpdateModeHeuristic);
#endif
    reader.setProperty("reader_progress_update", first);
    flush(first, {{first, heuristicText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute),
                 "Heuristic progress bar update did not use partial mode");
    reader.setProperty("reader_progress_update", first);
    flush(first, {{first, bit(gl16Attribute) | bit(fullAttribute)}});
    expectPolicy(bit(gl16Attribute) | bit(fullAttribute), "Progress mode changed an explicit waveform");
    reader.setProperty("reader_progress_update", first);
    flush(first, {{first, bit(gc16Attribute) | bit(fullAttribute)}});
    expectPolicy(bit(gc16Attribute) | bit(fullAttribute), "Progress mode changed a cleanup waveform");
    reader.setProperty("reader_progress_update", first);
    flush(QRegion(reader.rect()), {{reader.rect(), lightText}});
    expectPolicy(lightText, "Progress mode changed a whole-screen submission");
    flush(split, {{first, lightText}, {second, bit(gl16Attribute) | bit(fullAttribute)}});
    require(submittedRegion == split && submittedFlags.size() == 2 && submittedFlags[0].second == lightText &&
                submittedFlags[1].second == (bit(gl16Attribute) | bit(fullAttribute)),
            "Mixed text waveforms were replaced with the ordinary policy");
    flush(split, {{first, lightText}, {second, bit(lightOrdinaryAttribute) | bit(partialAttribute)}});
    require(submittedRegion == split && submittedFlags.size() == 2 && submittedFlags[0].second == lightText &&
                submittedFlags[1].second == (bit(lightOrdinaryAttribute) | bit(partialAttribute)),
            "Mixed update modes were replaced with the ordinary policy");
    flush(split, {{first, bit(gl16Attribute) | bit(fullAttribute)}, {second, bit(gl16Attribute) | bit(fullAttribute)}});
    require(submittedRegion == split &&
                submittedFlags == FlushFlags({{first, bit(gl16Attribute) | bit(fullAttribute)},
                                              {second, bit(gl16Attribute) | bit(fullAttribute)}}),
            "Separated explicit waveform regions were expanded or changed");
    const QRect top(0, 0, reader.width(), 80), bottom(0, 500, reader.width(), 100);
    const QRegion bars = QRegion(top) | QRegion(bottom);
    flush(bars, {{top, lightText}, {bottom, lightText}});
    require(submittedRegion == bars && submittedFlags == FlushFlags({{top, lightText}, {bottom, lightText}}),
            "Top and bottom controls refreshed the unchanged middle");
    require(!submittedRegion.intersects(QRect(0, 80, reader.width(), 420)),
            "Panel opening included the middle of the screen");
    const int beforeMixedBars = calls;
    flush(bars, {{top, heuristicText}, {bottom, lightText}, {reader.rect(), heuristicText}});
    require(calls == beforeMixedBars + 2 && submittedRegion == bars,
            "Window-sized policy did not preserve separate top and bottom submissions");
    for (const auto &entry : submittedFlags)
        require(top.contains(entry.first) || bottom.contains(entry.first),
                "Window-sized policy refreshed the unchanged middle");
    {
        // A slider handle and its value label repaint together while dragging.
        // MTK must show them in one update, not one per rectangle.
        QWidget panel(&reader);
        panel.setGeometry(0, 300, reader.width(), 150);
        panel.show();
        app.processEvents();
        const QRect handle(200, 320, 150, 30), label(20, 360, 40, 20), text(0, 100, reader.width(), 100);
        const QRegion edit = QRegion(handle) | QRegion(label);
        const int beforePanel = calls;
        flush(edit, {{handle, lightText}, {label, lightText}});
        require(calls == beforePanel + 1 && submittedRegion == QRegion(edit.boundingRect()) &&
                    submittedFlags == FlushFlags({{edit.boundingRect(), lightText}}),
                "Damage inside one panel was not combined into one rectangle");
        const int beforeWindowPolicy = calls;
        flush(edit, {{handle, lightText}, {label, lightText}, {reader.rect(), lightText}});
        require(calls == beforeWindowPolicy + 1 && submittedRegion == QRegion(edit.boundingRect()),
                "Window-sized policy split damage inside one panel");
        // A preview frame and its font sample arrive together. The text keeps
        // its own rectangle and the panel adds one more.
        flush(QRegion(text) | edit, {{text, lightText}, {handle, lightText}, {label, lightText}});
        require(submittedRegion == (QRegion(text) | QRegion(edit.boundingRect())) && submittedFlags.size() == 2,
                "Text and panel damage were merged or split");
        require(!submittedRegion.intersects(QRect(0, 200, reader.width(), 100)),
                "Panel damage refreshed unchanged text above the panel");
        // In an open appearance panel, a slider row or font sample updates
        // its changed pixels without flashing. The page and the whole panel
        // keep their policy.
        const quint64 panelPartial = (lightText & ~(bit(fullAttribute))) | bit(partialAttribute);
        reader.setProperty("reader_partial_controls", panel.geometry());
        flush(QRegion(handle), {{handle, lightText}});
        require(submittedFlags == FlushFlags({{handle, panelPartial}}), "A slider row inside the panel flashed");
        flush(QRegion(text) | QRegion(handle), {{text, lightText}, {handle, lightText}});
        require(submittedFlags.contains({text, lightText}) && submittedFlags.contains({handle, panelPartial}),
                "The page beside a panel edit changed policy, or the edit flashed");
        flush(QRegion(panel.geometry()), {{panel.geometry(), lightText}});
        require(submittedFlags == FlushFlags({{panel.geometry(), lightText}}),
                "Showing the whole panel lost its policy");
        reader.setProperty("reader_dark", true);
        flush(QRegion(handle), {{handle, darkOrdinary}}); // Entering dark mode cleans the screen once.
        flush(QRegion(handle), {{handle, darkOrdinary}});
        require(submittedFlags == FlushFlags({{handle, darkOrdinary}}), "A dark panel edit changed policy");
        reader.setProperty("reader_dark", false);
        reader.setProperty("reader_partial_controls", QRect());
    }

    // Adjacent widget rectangles can become one QRegion rectangle while the
    // flag table still contains one entry per widget. Count those entries too.
    const QRect adjacent(first.right() + 1, first.top(), first.width(), first.height());
    const QRegion joined = QRegion(first) | QRegion(adjacent);
    require(joined.rectCount() == 1, "Adjacent test damage did not form one rectangle");
    const int beforeJoined = policyRectangles;
    flush(joined, {{first, lightText}, {adjacent, lightText}});
    std::printf("Adjacent text damage: %d native policy rectangles\n", policyRectangles - beforeJoined);
    require(policyRectangles == beforeJoined + 1, "One rectangular frame retained multiple native submissions");
    expectPolicy(lightText, "Adjacent text damage changed its policy");
    require(submittedRegion == joined && submittedFlags[0].first == joined.boundingRect(),
            "Adjacent text damage lost its full coverage");
    FlushFlags tiles;
    const QRect strip(0, 110, reader.width(), 20);
    for (int i = 0; i < 16; ++i) {
        tiles.append({QRect(i * strip.width() / 16, strip.top(), strip.width() / 16, strip.height()), lightText});
    }
    const int beforeTiles = policyRectangles;
    flush(QRegion(strip), tiles);
    require(policyRectangles == beforeTiles + 1, "Tiled controls retained multiple native submissions");
    expectPolicy(lightText, "Tiled controls changed their policy");
    require(submittedFlags[0].first == strip, "Tiled controls lost coverage");
    std::printf("Tiled text damage: 16 input rectangles, %d native policy rectangle\n", policyRectangles - beforeTiles);
    flush(joined,
          {{first, bit(gl16Attribute) | bit(fullAttribute)}, {adjacent, bit(gl16Attribute) | bit(fullAttribute)}});
    expectPolicy(bit(gl16Attribute) | bit(fullAttribute), "Adjacent explicit waveform was changed");
    const FlushFlags mixedJoined{{first, lightText}, {adjacent, bit(gl16Attribute) | bit(fullAttribute)}};
    flush(joined, mixedJoined);
    require(submittedFlags == mixedJoined, "Adjacent mixed waveforms were merged");
    const FlushFlags mixedModes{{first, lightText}, {adjacent, bit(lightOrdinaryAttribute) | bit(partialAttribute)}};
    flush(joined, mixedModes);
    require(submittedFlags == mixedModes, "Adjacent mixed update modes were merged");
    const FlushFlags cleanJoined{{first, bit(imageAttribute) | bit(fullAttribute)},
                                 {adjacent, bit(imageAttribute) | bit(fullAttribute)}};
    flush(joined, cleanJoined);
    require(submittedFlags == cleanJoined, "Adjacent image cleanup policies were changed");
    const FlushFlags outside{{first, lightText}, {second, lightText}};
    flush(joined, outside);
    require(submittedFlags == outside, "Merging discarded a source rectangle outside the damage");
    const QRect inside(first.x() + 5, first.y() + 5, 10, 10);
    const FlushFlags incomplete{{first, lightText}, {inside, lightText}};
    flush(joined, incomplete);
    require(submittedFlags == incomplete, "Merging invented a policy for uncovered damage");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    nativeFlush32 = +[](QBackingStore *store, const QRegion &damage, QWindow *target, const QPoint &offset,
                        const FlushFlags32 &flags) {
        FlushFlags widened;
        for (const auto &entry : flags) {
            widened.append({entry.first, entry.second});
        }
        capture(store, damage, target, offset, widened);
    };
    const quint32 legacyText = quint32(bit(132) | bit(129));
    const int beforeLegacy = policyRectangles;
    submittedRegion = {};
    submittedFlags.clear();
    flushReader32(&store, joined, nullptr, {}, {{first, legacyText}, {adjacent, legacyText}});
    require(policyRectangles == beforeLegacy + 1, "Legacy Qt5 adjacent controls retained multiple policies");
    expectPolicy(bit(138) | bit(fullAttribute), "Legacy Qt5 merge lost its 32-bit text policy");
#endif
    reader.setProperty("reader_progress_update", joined.boundingRect());
    flush(joined, {{first, heuristicText}, {adjacent, heuristicText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute), "Joined progress did not use one partial update");
    reader.setProperty("reader_progress_update", first);
    flush(joined, {{first, lightText}, {adjacent, lightText}});
    expectPolicy(lightText, "Joined damage outside progress changed its mode");
    reader.setProperty("reader_page_turn", true);
    flush(joined, {{first, lightText}, {adjacent, lightText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute),
                 "Joined page turn did not use one partial update");
    reader.setProperty("reader_dark", true);
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkClean, "Entering dark mode lost cleanup after merged controls");
    flush(joined, {{first, darkOrdinary}, {adjacent, darkOrdinary}});
    expectPolicy(darkOrdinary, "Joined dark controls changed their full text policy");
    reader.setProperty("reader_progress_update", joined.boundingRect());
    flush(joined, {{first, darkOrdinary}, {adjacent, darkOrdinary}});
    expectPolicy(darkOrdinary, "Joined progress changed a dark refresh");
    const FlushFlags darkCleanup{{first, darkClean}, {adjacent, darkClean}};
    flush(joined, darkCleanup);
    require(submittedFlags == darkCleanup, "Joined dark image cleanup policies were changed");
    reader.setProperty("reader_dark", false);
    reader.resize(600, 400);
    const QRect landscapeLeft(10, 10, 250, 80), landscapeRight(260, 10, 250, 80);
    const QRegion landscapeDamage = QRegion(landscapeLeft) | QRegion(landscapeRight);
    flush(landscapeDamage, {{landscapeLeft, lightText}, {landscapeRight, lightText}});
    expectPolicy(lightText, "Landscape controls did not merge");
    require(submittedRegion == landscapeDamage && submittedFlags[0].first == landscapeDamage.boundingRect(),
            "Landscape merging changed the screen coordinates");
    reader.resize(400, 600);
    page.show();
    app.processEvents();
    reader.setProperty("reader_page_turn", true);
    reader.setProperty("reader_mono_repairs", QVariant::fromValue(QRegion(first)));
    flush(split, {{first, lightText}, {second, lightText}});
    require(submittedRegion == QRegion(reader.rect()), "Image-quality turn did not clean the entire screen");
    expectPolicy(bit(imageAttribute) | bit(fullAttribute) | bit(start),
                 "Image-quality turn lost native cleanup policy");
    reader.setProperty("reader_page_turn", true);
    reader.setProperty("reader_animation_direction", 1);
    animationAvailable = true;
    flush(second, {{second, lightText}});
    require(submittedRegion == QRegion(reader.rect()),
            "Outgoing image outside damage did not trigger whole-screen cleanup");
    expectPolicy(bit(imageAttribute) | bit(fullAttribute) | bit(start),
                 "Animated image turn did not select one cleanup");
    require(animationArms == 0, "Image-quality turn armed a sweep");
    animationAvailable = false;
    reader.setProperty("reader_mono_repairs", QVariant::fromValue(QRegion()));
    reader.setProperty("reader_animation_direction", 1);
    flush(first, {{first, lightText}});
    require(animationArms == 0, "Unpainted backing store armed an animation");
    const FlushFlags nativeFlags{{first, bit(gc16Attribute) | bit(fullAttribute)}};
    QWindow otherWindow;
    QBackingStore otherStore(&otherWindow);
    flushReader(&otherStore, first, nullptr, {}, nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "Another backing store was changed");
    flushReader(&store, first, &otherWindow, {}, nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "Another target window was changed");
    flushReader(&store, first, nullptr, QPoint(1, 0), nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "An offset flush was changed");
    reader.setProperty("reader_preparing_wake", true);
    const int beforeWake = calls;
    flushReader(&store, first, nullptr, {}, nativeFlags);
    require(calls == beforeWake, "Preparing wake submitted an intermediate reader frame");
    reader.setProperty("reader_preparing_wake", false);
    reader.setProperty("reader_preparing_handoff", true);
    const int beforeHandoff = calls;
    flushReader(&otherStore, first, nullptr, {}, nativeFlags);
    require(calls == beforeHandoff, "Preparing handoff submitted the background too early");
    flushReader(&otherStore, first, nullptr, QPoint(1, 0), nativeFlags);
    require(calls == beforeHandoff + 1, "Handoff preparation swallowed an unrelated offset flush");
    reader.setProperty("reader_preparing_handoff", false);
    QDialog modal(&reader);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.show();
    app.processEvents();
    require(QApplication::activeModalWidget() == &modal, "Other modal did not become active");
    flush(first, nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "Another modal's flush was changed");
    modal.hide();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    nativeRefreshFlags = +[](void *, quint64 &flags, const QRect &) {
        flags = 0x1234;
    };
    reader.setProperty("reader_exit_clean_pending", true);
    quint64 lookup = 0;
    lookupReaderRefreshFlags(nullptr, lookup, reader.rect());
    require(lookup == 0x1234 && reader.property("reader_exit_clean_pending").toBool(),
            "Visible reader consumed native exit cleanup");
    reader.hide();
    lookupReaderRefreshFlags(nullptr, lookup, first);
    require(lookup == 0x1234 && reader.property("reader_exit_clean_pending").toBool(),
            "Partial native damage consumed exit cleanup");
    lookupReaderRefreshFlags(nullptr, lookup, reader.rect());
    require(lookup == (bit(gc16Attribute) | bit(fullAttribute)) &&
                !reader.property("reader_exit_clean_pending").toBool(),
            "Hidden exit did not consume one native full cleanup");
    lookupReaderRefreshFlags(nullptr, lookup, reader.rect());
    require(lookup == 0x1234, "Exit cleanup changed a later native request");
#else
    reader.hide();
#endif
    reader.show();
    app.processEvents();
    setReaderRefreshWindow(&reader);
    reader.setProperty("reader_dark", false);
    reader.setProperty("reader_full_flash", false);
    reader.setProperty("reader_animation_direction", 1);
    store.resize(reader.size());
    store.beginPaint(QRegion(reader.rect()));
    store.endPaint();
    animationAvailable = true;
    const quint64 ordinary = bit(lightOrdinaryAttribute) | bit(fullAttribute);
    const QRect upper(0, 0, reader.width(), reader.height() / 2);
    const QRect lower(0, upper.height(), reader.width(), reader.height() - upper.height());
    flush(QRegion(reader.rect()), {{upper, ordinary}, {lower, ordinary}});
    require(submittedRegion == QRegion(reader.rect()), "Sweep did not publish the full backing store");
    expectPolicy(ordinary | bit(start), "Animation phase omitted the synchronous screen flush flag");
    require(animationArms == 1, "Animation phase was not armed once");
    animationAvailable = false;
    reader.setProperty("reader_plain_controls", false);
    for (const char *backend : {"mxc_epdc_fb", "unknown"}) {
        reader.setProperty("reader_refresh_backend", backend);
        setReaderRefreshWindow(&reader);
        pageRefresh(&page, false, true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        const quint64 input = bit(132) | bit(129);
#else
        const quint64 input = bit(Qt::WA_KoboEpdWfModeAutoText) | bit(Qt::WA_KoboEpdUpdateModeHeuristic);
#endif
        flush(first, {{first, input}});
        const auto baseline = submittedFlags;
        reader.setProperty("reader_progress_update", first);
        flush(first, {{first, input}});
        require(submittedFlags == baseline, "Progress request changed a non-MTK refresh");
        require(reader.property("reader_progress_update").toRect().isEmpty(),
                "Non-MTK flush retained a progress request");
        flush(split, {{first, input}, {second, input}});
        require(submittedFlags.size() == 1 && submittedRegion == QRegion(split.boundingRect()),
                "Fallback control update changed its existing merged coverage");
        const quint64 policy = submittedFlags[0].second;
        reader.setProperty("reader_page_turn", true);
        flush(split, {{first, input}, {second, input}});
        require(submittedRegion == QRegion(reader.rect()), "Fallback turn did not cover the whole screen");
        expectPolicy(policy, "Fallback turn changed the existing mode or waveform");
        require(!reader.property("reader_page_turn").toBool(), "Fallback turn request was not consumed");
    }
    setReaderRefreshWindow(nullptr);
    std::puts("PASS: dark refresh policies, animation suppression and native flush scope");
    return 0;
}
