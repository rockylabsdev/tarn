// Trim on an MP4 Recording, through the core seam: Handles that snap to
// whole frames, the ffmpeg command line, a frame-exact cut (the REAL ffmpeg on a
// committed 64x48 clip whose every frame carries its own number), progress and
// Cancel (a stub ffmpeg that is slow on purpose), and the rule that a file that
// was already saved or copied never changes.
#include "test_env.h"

#include "recording/recording_store.h"
#include "thumbnails/thumbnail_stack.h"
#include "trim/frame_cache.h"
#include "trim/trim.h"
#include "trim/trim_math.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTest>
#include <sys/stat.h>

using tarn::ThumbnailStack;
using tarn::Trim;
using tarn::TrimSession;
using tarn::test::TestEnv;

namespace {

// The committed clip: 3 s at 60 fps (180 frames), 64x48. Frame N paints N in binary
// as eight 8-px-wide black/white blocks, so a lossy re-encode still reads back.
QByteArray clip60() {
    return TestEnv::readFile(QStringLiteral(TARN_STUB_DIR) + "/fixtures/gpu-screen-recorder/sample60.mp4");
}

// How long a wait on the visible result may take. A Trim runs the real ffmpeg (and a GIF
// the stubbed gifski after it), which a loaded machine can stretch past QTRY's 5 s default
// (8 s seen with the CPUs busy). A passing wait still returns the moment it is true, so a
// long limit only matters when the result never comes.
constexpr int kTryMs = 30000;

// Frame numbers painted in a video, in order, by decoding it with the real ffmpeg.
QList<int> frameNumbers(const QString &path) {
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"), {"-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "gray", "-"});
    ffmpeg.waitForFinished(20000);
    const QByteArray out = ffmpeg.readAllStandardOutput();
    QList<int> numbers;
    constexpr int w = 64, h = 48;
    for (qsizetype at = 0; at + w * h <= out.size(); at += w * h) {
        int n = 0;
        for (int bit = 0; bit < 8; ++bit)
            if (quint8(out[at + (h / 2) * w + bit * 8 + 4]) > 128)
                n |= 1 << bit;
        numbers << n;
    }
    return numbers;
}

// The frame number painted in a cached preview picture (any size: the eight blocks scale with it).
int numberIn(const QImage &image) {
    int n = 0;
    for (int bit = 0; bit < 8; ++bit)
        if (qGray(image.pixel((image.width() * (2 * bit + 1)) / 16, image.height() / 2)) > 128)
            n |= 1 << bit;
    return n;
}

bool sameInode(const QString &a, const QString &b) {
    struct stat sa, sb;
    return ::stat(QFile::encodeName(a).constData(), &sa) == 0 && ::stat(QFile::encodeName(b).constData(), &sb) == 0 &&
           sa.st_ino == sb.st_ino && sa.st_dev == sb.st_dev;
}

} // namespace

class TrimTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    Trim &trim() { return env->daemon().service<Trim>(); }

    // A Recording on the stack whose kept file is the 60 fps clip.
    int recording(QString *keptPath = nullptr) {
        QString error;
        const QString path = env->daemon().service<tarn::RecordingStore>().newKeptPath(env->daemon().now(), "mp4", &error);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(clip60()) <= 0)
            return -1;
        file.close();
        if (keptPath)
            *keptPath = path;
        return stack().addRecording(TestEnv::defaultGrimPng(), env->daemon().now(), path, 3000);
    }
    // A GIF Recording: the kept 60 fps MP4 and the GIF beside it (the stack entry's videoPath is the GIF).
    int gifRecording(QString *keptMp4 = nullptr, QString *gifPath = nullptr) {
        QString mp4;
        recording(&mp4);
        const QString gif = mp4.chopped(3) + "gif";
        QFile file(gif);
        if (!file.open(QIODevice::WriteOnly) || file.write("OLD-GIF") <= 0)
            return -1;
        file.close();
        if (keptMp4)
            *keptMp4 = mp4;
        if (gifPath)
            *gifPath = gif;
        // The first Recording of the helper stays on the stack too; this is the GIF one.
        return stack().addRecording(TestEnv::defaultGrimPng(), env->daemon().now(), gif, 3000, mp4);
    }
    // Calls of the ffmpeg stub that wrote a trim (the ones with `-crf`).
    QList<QStringList> encodes() {
        QList<QStringList> out;
        for (const auto &call : env->calls("ffmpeg"))
            if (call.contains("-crf"))
                out << call;
        return out;
    }
    QStringList notifications() {
        QStringList out;
        for (const auto &call : env->calls("notify-send"))
            out << call.join(' ');
        return out;
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)));
    }
    void cleanup() { env.reset(); }

    // ── Opening ──────────────────────────────────────────────────────────────────────

    void trimOnARecordingOpensTheTrimWindowAndHidesTheThumbnail() {
        const int id = recording();
        QSignalSpy opened(&trim(), &Trim::opened);
        stack().edit(id); // a click on the Thumbnail, or its Trim button
        QCOMPARE(opened.size(), 1);
        auto *session = trim().session(id);
        QVERIFY(session);
        QVERIFY(stack().isHidden(id));
        QCOMPARE(session->frameCount(), 180);
        QCOMPARE(session->startFrame(), 0);
        QCOMPARE(session->endFrame(), 180);
        QVERIFY(session->openingSize().width() >= 1180); // the Editor's rule
    }

    void aScreenshotDoesNotOpenTrim() {
        const int shot = stack().add(TestEnv::defaultGrimPng(), env->daemon().now());
        stack().edit(shot);
        QVERIFY(!trim().session(shot));
        QVERIFY(!trim().open(shot));
    }

    void aGifRecordingOpensTrimOnItsKeptMp4At20Fps() {
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        auto *s = trim().open(id);
        QVERIFY(s);
        QVERIFY(s->isGif());
        QCOMPARE(s->fps(), 20);
        QCOMPARE(s->videoPath(), kept); // the cut is made on the MP4, never on the GIF
        QCOMPARE(s->frameCount(), 60);  // 3 s at 20 fps
        QVERIFY(stack().isHidden(id));
    }

    void aGifRecordingWithoutItsMp4DoesNotOpen() {
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        QVERIFY(QFile::remove(kept));
        QVERIFY(!trim().open(id));
    }

    // ── Frame snapping ───────────────────────────────────────────────────────────────

    void handlesSnapToWholeFramesAtSixtyFps() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0); // exactly frame 60
        QCOMPARE(s->startFrame(), 60);
        s->dragStart(1008.0); // 60.48 frames: down
        QCOMPARE(s->startFrame(), 60);
        s->dragStart(1009.0); // 60.54: up
        QCOMPARE(s->startFrame(), 61);
        s->dragEnd(2000.0);
        QCOMPARE(s->endFrame(), 120);
        s->dragEnd(1991.0); // 119.46
        QCOMPARE(s->endFrame(), 119);
        // Never outside the clip.
        s->dragStart(-500);
        QCOMPARE(s->startFrame(), 0);
        s->dragEnd(99999);
        QCOMPARE(s->endFrame(), 180);
    }

    void atLeastOneFrameIsAlwaysKept() {
        auto *s = trim().open(recording());
        s->dragEnd(1000.0);
        s->dragStart(5000.0); // far past the end
        QCOMPARE(s->startFrame(), 59);
        QCOMPARE(s->keptFrames(), 1);
        s->dragEnd(0.0);
        QCOMPARE(s->endFrame(), 60);
    }

    void thePreviewFollowsTheHandleAndThePlayheadSitsOnItAfterwards() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        QCOMPARE(s->previewFrame(), 60); // the first kept frame
        QCOMPARE(s->playheadFrame(), 60);
        s->dragEnd(2000.0);
        QCOMPARE(s->previewFrame(), 119); // the last kept frame, not the one after
        QCOMPARE(s->playheadFrame(), 120);
        s->scrub(1500.0);
        QCOMPARE(s->previewFrame(), 90);
        s->scrub(5000.0); // the Playhead stays inside the kept range
        QCOMPARE(s->playheadFrame(), 120);
        s->scrub(0.0);
        QCOMPARE(s->playheadFrame(), 60);
    }

    void startHereAndEndHereUseThePlayhead() {
        auto *s = trim().open(recording());
        s->scrub(500.0);
        s->startHere();
        QCOMPARE(s->startFrame(), 30);
        s->scrub(2500.0);
        s->endHere();
        QCOMPARE(s->endFrame(), 150);
    }

    void timesAreShownAsMinutesSecondsAndHundredths() {
        QCOMPARE(tarn::trim::formatTime(264, 60), QString("0:04.40"));
        QCOMPARE(tarn::trim::formatTime(744, 60), QString("0:12.40"));
        QCOMPARE(tarn::trim::formatTime(456, 60), QString("0:07.60"));
        QCOMPARE(tarn::trim::formatTime(60 * 75, 60), QString("1:15.00"));
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->dragEnd(2500.0);
        QCOMPARE(s->keptText(), QString("0:01.50"));
        QCOMPARE(s->durationText(), QString("0:03.00"));
    }

    // ── The preview's frame cache ─────────────────────────────────────────────

    // ffmpeg runs that decode the whole clip into the cache (raw frames on stdout).
    int cacheDecodes() {
        int n = 0;
        for (const auto &call : env->calls("ffmpeg"))
            if (call.contains("rawvideo"))
                ++n;
        return n;
    }

    void theCacheIsDecodedOnceInTheBackgroundWhenTrimOpensAndNeverBlocksOpening() {
        auto *s = trim().open(recording());
        QVERIFY(s);
        // Opening returned at once; the picture does not exist yet and the cache says so.
        QVERIFY(!s->cacheHasFrames() || s->frameCache().running() || s->frameCache().complete());
        QTRY_VERIFY_WITH_TIMEOUT(s->frameCache().complete(), kTryMs);
        QCOMPARE(s->frameCache().count(), 180);
        QCOMPARE(cacheDecodes(), 1);
    }

    void scrubbingDraggingAndPlaybackShowCachedFramesWithNoFfmpegRunPerFrame() {
        auto *s = trim().open(recording());
        QTRY_VERIFY_WITH_TIMEOUT(s->frameCache().complete(), kTryMs);
        const auto before = env->calls("ffmpeg").size();
        // Scrub across the whole clip, drag both Handles, step, and play two loops.
        for (int ms = 0; ms < 3000; ms += 25) {
            s->scrub(ms);
            const QImage shown = s->cachedFrame(s->previewFrame());
            QVERIFY(!shown.isNull());
            QCOMPARE(numberIn(shown), int(s->previewFrame()));
            QVERIFY(s->previewExact());
        }
        for (int ms = 0; ms < 1500; ms += 20)
            s->dragStart(ms);
        for (int ms = 3000; ms > 1600; ms -= 20)
            s->dragEnd(ms);
        s->stepFrames(1);
        s->stepSeconds(-1);
        s->toStart();
        s->play();
        for (int tick = 0; tick < 250; ++tick) { // 4 s of 16 ms ticks
            s->advance(16);
            const QImage shown = s->cachedFrame(s->previewFrame());
            QVERIFY(!shown.isNull());
            QCOMPARE(numberIn(shown), int(s->previewFrame()));
        }
        QCOMPARE(env->calls("ffmpeg").size(), before); // not one more run
    }

    void playbackStaysOnTimeWithTheCacheDecodedOrNot() {
        auto *s = trim().open(recording());
        s->play();
        // The clock counts whole milliseconds, whether or not any frame has been decoded yet.
        for (int i = 0; i < 100; ++i)
            s->advance(10);
        QCOMPARE(s->playheadFrame(), 60);
        s->pause(); // the real 16 ms clock must not run while the test waits
        QTRY_VERIFY_WITH_TIMEOUT(s->frameCache().complete(), kTryMs);
        s->play();
        for (int i = 0; i < 100; ++i)
            s->advance(10);
        QCOMPARE(s->playheadFrame(), 120);
        QCOMPARE(numberIn(s->cachedFrame(s->previewFrame())), 120);
    }

    void untilTheCacheIsReadyTheNearestDecodedFrameShowsNeverABlankOne() {
        auto *s = trim().open(recording());
        QTRY_VERIFY_WITH_TIMEOUT(s->cacheHasFrames(), kTryMs);
        s->scrub(2900); // far beyond what is decoded, if decoding is still going
        const QImage shown = s->cachedFrame(s->previewFrame());
        QVERIFY(!shown.isNull());
        QVERIFY(shown.width() > 0);
        QVERIFY(s->frameCache().count() > 0);
    }

    void theCacheRespectsItsMemoryCapAndKeepsEveryNthFrameWhenTheClipDoesNotFit() {
        const qint64 budget = 40 * 1024; // far less than 180 frames at any size
        trim().setCacheBudget(budget);
        auto *s = trim().open(recording());
        QTRY_VERIFY_WITH_TIMEOUT(s->frameCache().complete(), kTryMs);
        const auto &cache = s->frameCache();
        QVERIFY(cache.currentPlan().stride > 1);
        QVERIFY(cache.bytes() <= budget);
        QCOMPARE(cache.count(), cache.currentPlan().frames);
        QCOMPARE(cacheDecodes(), 1);
        // Every frame still has a picture: the nearest cached one, and it says when it is not exact.
        const int stride = cache.currentPlan().stride;
        for (int frame = 0; frame < 180; frame += 7) {
            const QImage shown = cache.frameAt(frame);
            QVERIFY(!shown.isNull());
            // (The pictures are tiny here, so read them against the first one: the cached frames are `stride` apart.)
            const int expected = int(qMin<qint64>(qRound(double(frame) / stride), cache.count() - 1)) * stride;
            QCOMPARE(shown, cache.frameAt(expected));
            QCOMPARE(numberIn(shown) - numberIn(cache.frameAt(0)), expected);
            QCOMPARE(cache.isExact(frame), frame % stride == 0);
        }
    }

    void thePlanPicksTheLargestSizeThatFitsThenDropsFramesForALongClip() {
        using tarn::FrameCache;
        const QSize box(1280, 720);
        // 13 s at 20 fps of 1440p fits the default budget (at some size).
        const auto gif = FrameCache::plan(QSize(2560, 1440), 260, box, FrameCache::kDefaultBudget);
        QCOMPARE(gif.stride, 1);
        QCOMPARE(gif.frames, 260);
        QVERIFY(gif.bytes <= FrameCache::kDefaultBudget);
        QVERIFY(gif.size.width() % 2 == 0 && gif.size.height() % 2 == 0);
        // A short clip is held at the full display size.
        const auto small = FrameCache::plan(QSize(2560, 1440), 60, box, FrameCache::kDefaultBudget);
        QCOMPARE(small.size, QSize(1280, 720));
        QCOMPARE(small.stride, 1);
        // 3 minutes at 60 fps cannot be held whole: every Nth frame, still within the cap.
        const auto longClip = FrameCache::plan(QSize(2560, 1440), 10800, box, FrameCache::kDefaultBudget);
        QVERIFY(longClip.stride > 1);
        QVERIFY(longClip.bytes <= FrameCache::kDefaultBudget);
        QCOMPARE(longClip.frames, (10800 + longClip.stride - 1) / longClip.stride);
        // Never enlarged past the video.
        QCOMPARE(FrameCache::plan(QSize(64, 48), 10, box, FrameCache::kDefaultBudget).size, QSize(64, 48));
    }

    void aGifRecordingCachesItsKeptMp4At20Fps() {
        QString mp4;
        const int id = gifRecording(&mp4);
        auto *s = trim().open(id);
        QVERIFY(s && s->isGif());
        QTRY_VERIFY_WITH_TIMEOUT(s->frameCache().complete(), kTryMs);
        QCOMPARE(s->frameCache().count(), 60); // 3 s at 20 fps
        const auto call = env->calls("ffmpeg").last().join(' ');
        QVERIFY2(call.contains("fps=20/1"), qPrintable(call));
        s->scrub(1500); // frame 30 of 60
        QVERIFY(qAbs(numberIn(s->cachedFrame(s->previewFrame())) - 90) <= 3); // 20 fps frame 30 = 60 fps frame 90
    }

    // ── The ffmpeg arguments ─────────────────────────────────────────────────────────

    void doneRunsX264VeryfastCrf20OnTheKeptCopyIntoAWorkFile() {
        QString kept;
        auto *s = trim().open(recording(&kept));
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming() && !s->isOpen(), kTryMs);
        const auto calls = encodes();
        QCOMPARE(calls.size(), 1);
        const QStringList &args = calls.first();
        auto after = [&](const QString &flag) { return args.value(args.indexOf(flag) + 1); };
        QCOMPARE(after("-i"), kept);
        QCOMPARE(after("-c:v"), QString("libx264"));
        QCOMPARE(after("-preset"), QString("veryfast"));
        QCOMPARE(after("-crf"), QString("20"));
        // Half a frame early at both ends: 59.5 / 60 s and 119.5 / 60 s.
        QCOMPARE(after("-vf"), QString("trim=start=0.991667:end=1.991667,setpts=PTS-STARTPTS"));
        // It writes beside, never to, the kept file; the kept path is swapped in afterwards.
        QVERIFY(args.last() != kept);
        QVERIFY(args.last().contains("trim-work"));
        QVERIFY(QFileInfo::exists(kept));
        QVERIFY(!QFileInfo::exists(args.last()));
    }

    void theNewPosterIsMadeInTheBackgroundAfterTheSwap() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        env->setFixture("ffmpeg", "poster_delay", "1"); // slows the poster frame, not the cut
        s->done();
        // The cut is quick; then the poster takes a second while the daemon keeps answering.
        QTRY_VERIFY_WITH_TIMEOUT(env->calls("ffmpeg").size() >= 2, kTryMs);
        QElapsedTimer clock;
        clock.start();
        QVERIFY(env->request({"settings"}).ok);
        QVERIFY(clock.elapsed() < 800);
        QVERIFY(s->trimming()); // still finishing
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming() && !s->isOpen(), kTryMs);
    }

    // ── A frame-exact cut (the real ffmpeg) ──────────────────────────────────────────

    void theClipReallyHasOneNumberedFramePerSixtieth() {
        QString kept;
        recording(&kept);
        const QList<int> all = frameNumbers(kept);
        QCOMPARE(all.size(), 180);
        for (int i = 0; i < 180; ++i)
            QCOMPARE(all[i], i);
    }

    void doneKeepsExactlyTheFramesBetweenTheHandles() {
        QString kept;
        const int id = recording(&kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0); // frame 60
        s->dragEnd(2000.0);   // up to frame 119
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        const QList<int> numbers = frameNumbers(kept);
        QCOMPARE(numbers.size(), 60);
        QCOMPARE(numbers.first(), 60);
        QCOMPARE(numbers.last(), 119);
        for (int i = 0; i < numbers.size(); ++i)
            QCOMPARE(numbers[i], 60 + i);
        // The Thumbnail is back, shorter, with a new poster.
        QVERIFY(!stack().isHidden(id));
        QCOMPARE(stack().durationMs(id), 1000);
        QCOMPARE(stack().chip(id), QString("MP4 0:01"));
        QVERIFY(notifications().isEmpty());
    }

    void anUnevenCutIsExactToo() {
        QString kept;
        auto *s = trim().open(recording(&kept));
        s->setStartFrame(1);
        s->setEndFrame(180);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        const QList<int> numbers = frameNumbers(kept);
        QCOMPARE(numbers.size(), 179);
        QCOMPARE(numbers.first(), 1);
        QCOMPARE(numbers.last(), 179);
        // And trimming the trimmed copy again counts from its own start.
        auto *again = trim().open(stack().ids().last());
        QVERIFY(again);
        QCOMPARE(again->frameCount(), 179);
        again->setEndFrame(10);
        again->done();
        QTRY_VERIFY_WITH_TIMEOUT(!again->isOpen(), kTryMs);
        QCOMPARE(frameNumbers(kept), (QList<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
    }

    void doneWithNothingCutJustClosesAndWritesNothing() {
        QString kept;
        const int id = recording(&kept);
        const QByteArray before = TestEnv::readFile(kept);
        auto *s = trim().open(id);
        s->done();
        QVERIFY(!s->isOpen());
        QVERIFY(encodes().isEmpty());
        QCOMPARE(TestEnv::readFile(kept), before);
        QVERIFY(!stack().isHidden(id));
    }

    void aFailedEncodeKeepsTheKeptCopyAndSaysSo() {
        QString kept;
        const int id = recording(&kept);
        const QByteArray before = TestEnv::readFile(kept);
        env->setMode("ffmpeg", "error");
        auto *s = trim().open(id);
        s->dragEnd(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming(), kTryMs);
        QVERIFY(s->isOpen()); // still there to try again
        QCOMPARE(TestEnv::readFile(kept), before);
        QCOMPARE(notifications().size(), 1);
        QVERIFY2(notifications().first().contains("Tarn could not trim the recording"), qPrintable(notifications().join('|')));
        QVERIFY(TestEnv::filesIn(Trim::workDir()).isEmpty());
        QCOMPARE(s->endFrame(), 60); // the range is still what the user set
    }

    // ── Progress and Cancel ──────────────────────────────────────────────────────────

    void progressFollowsFfmpegsOutputTime() {
        env->setMode("ffmpeg", "fake");
        // 2 s kept = 2 000 000 us: a quarter, a half, three quarters.
        env->setFixture("ffmpeg", "progress_us", "500000 1000000 1500000");
        QString kept;
        const int id = recording(&kept);
        auto *s = trim().open(id);
        s->dragStart(500.0);
        s->dragEnd(2500.0);
        QCOMPARE(s->keptFrames(), 120);
        QList<qreal> seen;
        connect(s, &TrimSession::progressChanged, this, [&] { seen << s->progress(); });
        s->done();
        QVERIFY(s->trimming());
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QVERIFY2(seen.contains(0.25) && seen.contains(0.5) && seen.contains(0.75), qPrintable(QString::number(seen.size())));
        QVERIFY(std::is_sorted(seen.begin(), seen.end()));
        QVERIFY(!stack().isHidden(id));
        QCOMPARE(stack().durationMs(id), 2000);
    }

    void cancelStopsFfmpegAndLeavesTheKeptCopyAsItWas() {
        env->setMode("ffmpeg", "fake");
        env->setFixture("ffmpeg", "step_delay", "30");
        QString kept;
        const int id = recording(&kept);
        const QByteArray before = TestEnv::readFile(kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(s->trimming(), kTryMs);
        QTRY_COMPARE_WITH_TIMEOUT(encodes().size(), 1, kTryMs);
        s->cancelTrim();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming(), kTryMs);
        QVERIFY(s->isOpen());              // back to editing, nothing lost
        QCOMPARE(s->startFrame(), 60);     // the Handles stay
        QCOMPARE(s->progress(), 0.0);
        QCOMPARE(TestEnv::readFile(kept), before);
        QVERIFY(TestEnv::filesIn(Trim::workDir()).isEmpty());
        QVERIFY(stack().isHidden(id));
        QVERIFY(notifications().isEmpty()); // canceling is not an error
        // It can be done again afterwards.
        env->setMode("ffmpeg", "ok");
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QCOMPARE(frameNumbers(kept).first(), 60);
    }

    void handlesAndKeysDoNothingWhileTrimming() {
        env->setMode("ffmpeg", "fake");
        env->setFixture("ffmpeg", "step_delay", "30");
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(s->trimming(), kTryMs);
        s->dragStart(0.0);
        s->dragEnd(1200.0);
        QCOMPARE(s->startFrame(), 60);
        QCOMPARE(s->endFrame(), 180);
        s->done(); // no second encode
        QTest::qWait(100);
        QCOMPARE(encodes().size(), 1);
        s->cancelTrim();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming(), kTryMs);
    }

    // ── Esc ──────────────────────────────────────────────────────────────────────────

    void escClosesTheWindowAndDropsTheTrim() {
        QString kept;
        const int id = recording(&kept);
        const QByteArray before = TestEnv::readFile(kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        QSignalSpy finished(s, &TrimSession::finished);
        s->cancel();
        QCOMPARE(finished.size(), 1);
        QVERIFY(!stack().isHidden(id));
        QVERIFY(encodes().isEmpty());
        QCOMPARE(TestEnv::readFile(kept), before);
        QVERIFY(!trim().session(id));
        QVERIFY(trim().open(id)); // opens fresh, whole clip
        QCOMPARE(trim().session(id)->startFrame(), 0);
    }

    void escWhileTrimmingStopsItAndKeepsTheOriginal() {
        env->setMode("ffmpeg", "fake");
        env->setFixture("ffmpeg", "step_delay", "30");
        QString kept;
        const int id = recording(&kept);
        const QByteArray before = TestEnv::readFile(kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(s->trimming(), kTryMs);
        s->cancel();
        QVERIFY(!stack().isHidden(id));
        QCOMPARE(TestEnv::readFile(kept), before);
        QTRY_VERIFY_WITH_TIMEOUT(TestEnv::filesIn(Trim::workDir()).isEmpty(), kTryMs);
    }

    void closingTheThumbnailFromElsewhereEndsTheWindow() {
        const int id = recording();
        auto *s = trim().open(id);
        QSignalSpy finished(s, &TrimSession::finished);
        stack().closeAll();
        QCOMPARE(finished.size(), 1);
        QVERIFY(!trim().session(id));
    }

    // ── Copy and Save as… from the window ────────────────────────────────────────────

    void copyFromTheWindowTrimsThenCopiesTheTrimmedFile() {
        QString kept;
        const int id = recording(&kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->copyAndClose();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QVERIFY(!stack().ids().contains(id)); // Thumbnail closed, like Copy on it
        const auto clips = env->calls("tarn-clip");
        QCOMPARE(clips.size(), 1);
        const QString shared = clips.first().last();
        QVERIFY(shared.contains("/shared/"));
        QCOMPARE(frameNumbers(shared).size(), 60);
        QCOMPARE(frameNumbers(shared).first(), 60);
    }

    void copyWithNothingCutCopiesAtOnce() {
        const int id = recording();
        auto *s = trim().open(id);
        s->copyAndClose();
        QVERIFY(!s->isOpen());
        QVERIFY(encodes().isEmpty());
        QCOMPARE(env->calls("tarn-clip").size(), 1);
        QVERIFY(!stack().ids().contains(id));
    }

    void saveAsFromTheWindowTrimsThenWritesWhereAsked() {
        QString kept;
        const int id = recording(&kept);
        auto *s = trim().open(id);
        s->dragStart(500.0);
        s->dragEnd(1500.0);
        const QString target = env->scratch() + "/home/Desktop/My clip.mp4";
        s->saveAs(target);
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        const QList<int> numbers = frameNumbers(target);
        QCOMPARE(numbers.size(), 60);
        QCOMPARE(numbers.first(), 30);
        QCOMPARE(numbers.last(), 89);
        QVERIFY(!stack().ids().contains(id));
        QVERIFY(TestEnv::filesIn(env->recordings()).isEmpty()); // not into the Recordings folder as well
    }

    void saveAsRemembersTheFolderForNextTime() {
        const int id = recording();
        auto *s = trim().open(id);
        s->saveAs(env->scratch() + "/home/Desktop/a.mp4");
        QVERIFY(QFileInfo::exists(env->scratch() + "/home/Desktop/a.mp4"));
        const int next = recording();
        QCOMPARE(trim().open(next)->saveAsFolder(), env->scratch() + "/home/Desktop");
    }

    // ── Never touch a file that was already saved or copied ──────────────────────────

    void aSavedFileIsUnchangedAfterTrim() {
        QString kept;
        const int id = recording(&kept);
        const QString saved = stack().save(id);
        QVERIFY(!saved.isEmpty());
        const QByteArray before = TestEnv::readFile(saved);
        QVERIFY(stack().restore()); // the Thumbnail comes back, still on the same kept file
        const int back = stack().ids().last();
        auto *s = trim().open(back);
        QVERIFY(s);
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QCOMPARE(frameNumbers(kept).size(), 60);
        QCOMPARE(TestEnv::readFile(saved), before);
        QCOMPARE(frameNumbers(saved).size(), 180);
    }

    void aCopiedFileIsUnchangedAfterTrimEvenThoughItWasAHardLinkOfTheKeptFile() {
        QString kept;
        const int id = recording(&kept);
        QVERIFY(stack().copy(id));
        const QString shared = env->calls("tarn-clip").first().last();
        QVERIFY2(sameInode(shared, kept), "Copy hands the clipboard a hard link; the trim must not write through it");
        const QByteArray before = TestEnv::readFile(shared);
        QVERIFY(stack().restore());
        auto *s = trim().open(stack().ids().last());
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QCOMPARE(TestEnv::readFile(shared), before);
        QCOMPARE(frameNumbers(shared).size(), 180);
        QVERIFY(!sameInode(shared, kept));
        QCOMPARE(frameNumbers(kept).size(), 120);
    }

    void copyingAfterTheTrimPastesTheTrimmedVersionNotTheOldOne() {
        QString kept;
        const int id = recording(&kept);
        QVERIFY(stack().copy(id));
        const QString first = env->calls("tarn-clip").first().last();
        QVERIFY(stack().restore());
        auto *s = trim().open(stack().ids().last());
        s->dragEnd(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QVERIFY(stack().copy(stack().ids().last()));
        const auto clips = env->calls("tarn-clip");
        QCOMPARE(clips.size(), 2);
        const QString second = clips.last().last();
        QVERIFY(second != first);
        QCOMPARE(frameNumbers(second).size(), 60);
        QCOMPARE(frameNumbers(first).size(), 180);
    }

    // ── Playback ─────────────────────────────────────────────────────────────────────

    void spacePlaysTheKeptRangeOnALoopByTheClock() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0); // frame 60
        s->dragEnd(2000.0);   // up to 120
        QVERIFY(!s->playing());
        s->togglePlay();      // Space
        QVERIFY(s->playing());
        QCOMPARE(s->playheadFrame(), 60);
        s->advance(500); // half a second = 30 frames
        QCOMPARE(s->playheadFrame(), 90);
        s->advance(500); // the end of the range wraps to its start
        QCOMPARE(s->playheadFrame(), 60);
        s->advance(1250); // 75 frames: one loop (60) and 15 more
        QCOMPARE(s->playheadFrame(), 75);
        s->togglePlay();
        QVERIFY(!s->playing());
        s->advance(500);
        QCOMPARE(s->playheadFrame(), 75); // paused: the clock does nothing
    }

    void playbackKeepsFractionsOfAFrameSoTimeDoesNotDrift() {
        auto *s = trim().open(recording());
        s->play();
        for (int i = 0; i < 100; ++i)
            s->advance(10); // 1 s in 10 ms ticks, each 0.6 frames
        QCOMPARE(s->playheadFrame(), 60);
    }

    void playbackStartsFromTheStartHandleWhenThePlayheadIsAtTheEnd() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->toEnd();
        QCOMPARE(s->playheadFrame(), 180);
        s->play();
        QCOMPARE(s->playheadFrame(), 60);
    }

    void playbackStaysInTheRangeWhenAHandleMovesUnderIt() {
        auto *s = trim().open(recording());
        s->play();
        s->advance(2000); // frame 120
        s->dragEnd(1000.0); // the end Handle comes in to frame 60
        s->advance(100);
        QVERIFY(s->playheadFrame() >= s->startFrame() && s->playheadFrame() < s->endFrame());
    }

    void theRealClockMovesThePlayhead() {
        auto *s = trim().open(recording());
        s->play();
        QTRY_VERIFY_WITH_TIMEOUT(s->playheadFrame() >= 6, 3000); // 100 ms of 60 fps
        s->pause();
    }

    void scrubbingMovesThePlayheadAndKeepsItInsideTheRange() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->scrub(1500.0);
        QCOMPARE(s->playheadFrame(), 90);
        s->scrub(100.0); // before the start Handle
        QCOMPARE(s->playheadFrame(), 60);
        s->scrub(2900.0);
        QCOMPARE(s->playheadFrame(), 120);
    }

    // ── Keys ─────────────────────────────────────────────────────────────────────────

    void iAndOSetTheHandlesAtThePlayhead() {
        auto *s = trim().open(recording());
        s->scrub(1000.0);
        s->startHere(); // I
        QCOMPARE(s->startFrame(), 60);
        s->scrub(2500.0);
        s->endHere(); // O
        QCOMPARE(s->endFrame(), 150);
        QCOMPARE(s->keptFrames(), 90);
    }

    void arrowsStepAFrameAndShiftArrowsASecond() {
        auto *s = trim().open(recording());
        s->scrub(1000.0);
        s->stepFrames(1);
        QCOMPARE(s->playheadFrame(), 61);
        s->stepFrames(-1);
        s->stepFrames(-1);
        QCOMPARE(s->playheadFrame(), 59);
        s->stepSeconds(1);
        QCOMPARE(s->playheadFrame(), 119);
        s->stepSeconds(-1);
        s->stepSeconds(-1);
        QCOMPARE(s->playheadFrame(), 0); // the clip's start stops it
    }

    void steppingStopsAtTheHandlesAndPausesPlayback() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->play();
        s->stepFrames(1);
        QVERIFY(!s->playing());
        s->stepSeconds(5);
        QCOMPARE(s->playheadFrame(), 120);
        s->stepSeconds(-5);
        QCOMPARE(s->playheadFrame(), 60);
    }

    void homeAndEndJumpToTheHandles() {
        auto *s = trim().open(recording());
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->scrub(1500.0);
        s->toEnd(); // End
        QCOMPARE(s->playheadFrame(), 120);
        QCOMPARE(s->previewFrame(), 119); // the last kept frame
        s->toStart(); // Home
        QCOMPARE(s->playheadFrame(), 60);
        s->play();
        s->toEnd();
        QVERIFY(!s->playing());
    }

    void keysAndPlaybackDoNothingWhileTrimming() {
        env->setMode("ffmpeg", "fake");
        env->setFixture("ffmpeg", "step_delay", "30");
        auto *t = trim().open(recording());
        t->dragStart(1000.0);
        t->done();
        QTRY_VERIFY_WITH_TIMEOUT(t->trimming(), kTryMs);
        t->play();
        t->stepFrames(5);
        t->toEnd();
        QVERIFY(!t->playing());
        QCOMPARE(t->playheadFrame(), 60);
        t->cancelTrim();
        QTRY_VERIFY_WITH_TIMEOUT(!t->trimming(), kTryMs);
    }

    // ── A GIF: 20 fps and the re-make ────────────────────────────────────────────────

    void aGifsHandlesSnapTo20Fps() {
        auto *s = trim().open(gifRecording());
        s->dragStart(1020.0); // 20.4 frames -> boundary 20
        QCOMPARE(s->startFrame(), 20);
        s->dragEnd(2480.0);   // 49.6 -> 50
        QCOMPARE(s->endFrame(), 50);
        s->dragStart(1990.0); // 39.8 -> 40
        QCOMPARE(s->startFrame(), 40);
        QCOMPARE(s->timeText(40), QString("0:02.00"));
        QCOMPARE(s->keptText(), QString("0:00.50"));
        s->stepFrames(1);
        QCOMPARE(s->playheadFrame(), 41);
        s->stepSeconds(1);
        QCOMPARE(s->playheadFrame(), 50); // a second is 20 frames, held by the end Handle
        s->scrub(1000.0);
        QCOMPARE(s->playheadFrame(), 40);
    }

    void aGifPlaysAt20FramesASecond() {
        auto *s = trim().open(gifRecording());
        s->play();
        s->advance(1000);
        QCOMPARE(s->playheadFrame(), 20);
    }

    void cuttingAGifMakesTheTrimmedMp4ThenTheGifFromItWithGifski() {
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        const int posterWidth = stack().image(id).width(); // the video's width, as Trim sees it
        auto *s = trim().open(id);
        s->dragStart(1000.0); // boundary 20 = 1 s
        s->dragEnd(2000.0);   // boundary 40 = 2 s
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);

        // 1. The kept MP4 was cut frame-exactly at 20 fps boundaries (frames 60..119 of 60 fps).
        const QList<int> numbers = frameNumbers(kept);
        QCOMPARE(numbers.size(), 60);
        QCOMPARE(numbers.first(), 60);
        QCOMPARE(numbers.last(), 119);
        // 2. THEN the GIF was re-made from that trimmed MP4 by ffmpeg | gifski (the GIF Recording pipeline).
        const auto ffmpegCalls = env->calls("ffmpeg");
        int cut = -1, remake = -1;
        for (int i = 0; i < ffmpegCalls.size(); ++i) {
            if (ffmpegCalls[i].contains("-crf") && cut < 0)
                cut = i;
            if (ffmpegCalls[i].contains("yuv4mpegpipe"))
                remake = i;
        }
        QVERIFY(cut >= 0);
        QVERIFY(remake > cut);
        const QStringList &pipe = ffmpegCalls[remake];
        QCOMPARE(pipe.value(pipe.indexOf("-vf") + 1), QString("fps=20,scale='min(iw,1280)':-2")); // the default limit
        QVERIFY(pipe.value(pipe.indexOf("-i") + 1).contains("trim-work")); // read from the trimmed work file
        const auto gifski = env->calls("gifski");
        QCOMPARE(gifski.size(), 1);
        QCOMPARE(gifski.first().mid(0, 4), (QStringList{"--fps", "20", "--width", QString::number(posterWidth)}));
        QCOMPARE(gifski.first().value(gifski.first().indexOf("-o") + 1), gif + ".part");
        const QByteArray y4m = env->stdinOfCall("gifski", 1);
        QVERIFY(y4m.startsWith("YUV4MPEG2"));
        QCOMPARE(y4m.count("FRAME"), 20); // one second at 20 fps
        // 3. The GIF Recording now holds the new GIF, and the cut made at the cut MP4.
        QCOMPARE(TestEnv::readFile(gif), TestEnv::readFile(QStringLiteral(TARN_STUB_DIR) + "/fixtures/gifski/sample.gif"));
        QVERIFY(!QFileInfo::exists(gif + ".part"));
        QVERIFY(!stack().isHidden(id));
        QCOMPARE(stack().durationMs(id), 1000);
        QCOMPARE(stack().chip(id), QString("GIF 0:01"));
        QVERIFY(TestEnv::filesIn(Trim::workDir()).isEmpty());
        QVERIFY(notifications().isEmpty());
    }

    void theGifRemakeRunsFfmpegAndGifskiUnderNice() {
        const int id = gifRecording();
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        bool ffmpeg = false, gifski = false;
        for (const auto &c : env->calls("nice")) {
            ffmpeg |= c.mid(0, 3) == QStringList{"-n", "10", "ffmpeg"} && c.contains("yuv4mpegpipe");
            gifski |= c.mid(0, 3) == QStringList{"-n", "10", "gifski"};
        }
        QVERIFY2(ffmpeg && gifski, qPrintable(QString::number(env->calls("nice").size()) + " nice calls"));
    }

    void aGifTrimRemakesTheGifToTheWidthLimitAndTheKeptMp4StaysFull() {
        QDir().mkpath(env->scratch() + "/config/tarn");
        QFile config(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("gif_max_width = 16\n");
        config.close();
        env->restartDaemon();
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        const int posterWidth = stack().image(id).width();
        QVERIFY(posterWidth > 16);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->dragEnd(2000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QStringList pipe;
        for (const auto &c : env->calls("ffmpeg"))
            if (c.contains("yuv4mpegpipe"))
                pipe = c;
        QCOMPARE(pipe.value(pipe.indexOf("-vf") + 1), QString("fps=20,scale='min(iw,16)':-2"));
        const auto gifski = env->calls("gifski").first();
        QCOMPARE(gifski.at(gifski.indexOf("--width") + 1), QString("16"));
        // The cut MP4 is not scaled: no scale filter in the cut.
        for (const auto &c : env->calls("ffmpeg"))
            if (c.contains("-crf"))
                QVERIFY(!c.join(' ').contains("scale"));
    }

    void gifProgressRunsThroughTheCutThenTheGif() {
        env->setFixture("gifski", "frames", "40");
        env->setFixture("gifski", "frame_delay", "0.05");
        auto *s = trim().open(gifRecording());
        s->dragStart(1000.0);
        s->done();
        QList<qreal> seen;
        connect(s, &TrimSession::progressChanged, this, [&] { seen << s->progress(); });
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QVERIFY(seen.size() > 2);
        for (int i = 1; i < seen.size(); ++i)
            QVERIFY2(seen[i] >= seen[i - 1], "progress never goes back");
        bool inSecondHalf = false;
        for (qreal p : seen)
            inSecondHalf |= p > 0.5 && p < 1.0;
        QVERIFY(inSecondHalf);
        QCOMPARE(seen.last(), 1.0);
    }

    void aGifskiFailureKeepsBothFilesAsTheyWereAndSaysSo() {
        env->setMode("gifski", "die");
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        const QByteArray mp4Before = TestEnv::readFile(kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming() && notifications().size() == 1, kTryMs);
        QVERIFY(s->isOpen());
        QVERIFY(notifications().first().contains("Tarn could not trim the recording"));
        QCOMPARE(TestEnv::readFile(kept), mp4Before);
        QCOMPARE(TestEnv::readFile(gif), QByteArray("OLD-GIF"));
        QVERIFY(!QFileInfo::exists(gif + ".part"));
        QVERIFY(TestEnv::filesIn(Trim::workDir()).isEmpty());
        QCOMPARE(s->startFrame(), 20); // still editing
    }

    void cancelingDuringTheGifLeavesEverythingAsItWas() {
        env->setFixture("gifski", "frames", "200");
        env->setFixture("gifski", "frame_delay", "0.1");
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        const QByteArray mp4Before = TestEnv::readFile(kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(s->progress() > 0.5, kTryMs); // in the GIF half
        s->cancelTrim();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming(), kTryMs);
        QVERIFY(s->isOpen());
        QCOMPARE(TestEnv::readFile(kept), mp4Before);
        QCOMPARE(TestEnv::readFile(gif), QByteArray("OLD-GIF"));
        QVERIFY(!QFileInfo::exists(gif + ".part"));
        QVERIFY(TestEnv::filesIn(Trim::workDir()).isEmpty());
        QVERIFY(notifications().isEmpty());
    }

    void aGifWithNothingCutJustClosesAndMakesNoNewGif() {
        const int id = gifRecording();
        auto *s = trim().open(id);
        s->done();
        QVERIFY(!s->isOpen());
        QVERIFY(env->calls("gifski").isEmpty());
        QVERIFY(!stack().isHidden(id));
    }

    void copyOfATrimmedGifCopiesTheNewGif() {
        QString kept, gif;
        const int id = gifRecording(&kept, &gif);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->copyAndClose();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isOpen(), kTryMs);
        QCOMPARE(env->calls("gifski").size(), 1);
        QVERIFY(!env->calls("tarn-clip").isEmpty());
    }

    void aCrashMidTrimLeavesNoHalfFileToBeRecovered() {
        env->setMode("ffmpeg", "fake");
        env->setFixture("ffmpeg", "step_delay", "30");
        QString kept;
        const int id = recording(&kept);
        auto *s = trim().open(id);
        s->dragStart(1000.0);
        s->done();
        QTRY_VERIFY_WITH_TIMEOUT(s->trimming(), kTryMs);
        // The work folder is not the kept folder, so recovery never saves a work file.
        QVERIFY(!Trim::workDir().startsWith(tarn::RecordingStore::keptDir()));
        QCOMPARE(TestEnv::filesIn(env->keptRecordings()).size(), 1);
        s->cancelTrim();
        QTRY_VERIFY_WITH_TIMEOUT(!s->trimming(), kTryMs);
    }
};

QTEST_MAIN(TrimTest)
#include "trim_test.moc"
