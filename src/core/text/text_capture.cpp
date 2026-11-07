#include "text_capture.h"

#include "clipboard/clipboard.h"
#include "daemon/daemon.h"
#include "notify/notifier.h"
#include "overlay/overlay.h"
#include "process/process_runner.h"
#include "settings/settings.h"
#include "state/remembered_state.h"

#include <QBuffer>
#include <QDir>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <memory>

namespace tarn {

namespace {
// Empty when the picture can't be encoded.
QByteArray encodePng(const QImage &image) {
    QByteArray png;
    QBuffer buffer(&png);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return png;
}

// Tesseract is tuned for scanned pages; code on screen (8-10 px tall) reads far
// better at 2x (about a fifth of the misreads, 3x no better). A large
// Selection is enlarged less, down to 1x, to stay under kMaxReadPixels.
constexpr double kReadScale = 2.0;
constexpr double kMaxReadPixels = 8'000'000;
double readScale(QSize size) {
    return std::clamp(std::sqrt(kMaxReadPixels / (double(size.width()) * size.height())), 1.0, kReadScale);
}
} // namespace

QString joinParagraphs(const QString &recognized) {
    QStringList paragraphs;
    QStringList lines;
    const auto endParagraph = [&] {
        if (!lines.isEmpty())
            paragraphs << lines.join(QLatin1Char(' '));
        lines.clear();
    };
    QString text = recognized;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\f'), QLatin1Char('\n')); // Tesseract ends each page with a form feed
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.simplified();
        if (line.isEmpty())
            endParagraph();
        else
            lines << line;
    }
    endParagraph();
    return paragraphs.join(QStringLiteral("\n\n"));
}

TextCapture::TextCapture(Daemon &daemon) : m_daemon(daemon) { m_pool.setMaxThreadCount(1); }

TextCapture::~TextCapture() { m_pool.waitForDone(); }

void TextCapture::capture(const QImage &selection) {
    if (selection.isNull()) {
        couldNotCutOut();
        return;
    }
    // The picture goes in on stdin and the text comes out on stdout. The
    // languages Setting and the Text layout are read now, so an edit applies at once.
    const QString languages = m_daemon.service<Settings>().textLanguages();
    const TextLayout layout =
        textLayoutFromKey(m_daemon.service<RememberedState>().value(QStringLiteral("text.layout"), QString()).toString());
    QStringList args{QStringLiteral("stdin"), QStringLiteral("stdout"), QStringLiteral("-l"), languages};
    if (layout != TextLayout::Paragraphs) // Lines and Table rebuild from word boxes
        args << QStringLiteral("--psm") << QStringLiteral("6") << QStringLiteral("tsv");
    ++m_running;

    // Scaling and encoding a full-screen Selection takes a while: off the daemon's
    // thread. ZXingReader reads the Selection as it is; only Tesseract's copy is
    // enlarged, in memory, never saved or shown.
    m_pool.start([this, selection, args, layout] {
        const QByteArray png = encodePng(selection);
        const double scale = readScale(selection.size());
        const QByteArray enlarged =
            scale <= 1.0 ? png
                         : encodePng(selection.scaled(int(selection.width() * scale), int(selection.height() * scale),
                                                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        QMetaObject::invokeMethod(this, [this, png, enlarged, args, layout] {
            if (png.isEmpty() || enlarged.isEmpty()) {
                --m_running;
                couldNotCutOut();
                return;
            }
            read(png, enlarged, args, layout);
        }, Qt::QueuedConnection);
    });
}

void TextCapture::couldNotCutOut() {
    m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not read the text"),
                                        QStringLiteral("the selected area could not be cut out"));
    emit finished({});
}

void TextCapture::read(const QByteArray &png, const QByteArray &enlargedPng, const QStringList &args, TextLayout layout) {
    // Two reads run side by side: Tesseract for the text and ZXingReader for QR
    // codes. Whichever ends last finishes the capture.
    struct Pending {
        int left = 2;
        ProcessResult text;
        QStringList codes;
        std::unique_ptr<QTemporaryFile> file; // ZXingReader reads a path, not stdin
    };
    auto pending = std::make_shared<Pending>();
    const auto oneDone = [this, pending, layout] {
        if (--pending->left > 0)
            return;
        --m_running;
        onRecognized(pending->text, pending->codes, layout);
    };

    auto &runner = m_daemon.service<ProcessRunner>();
    runner.runAsync(QStringLiteral("tesseract"), args, enlargedPng, 60000, this, [pending, oneDone](const ProcessResult &result) {
        pending->text = result;
        oneDone();
    });

    // Without ZXingReader installed (or if the picture can't be written) there
    // are simply no codes, and Text capture works as it always did.
    pending->file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/tarn-qr-XXXXXX.png"));
    if (!pending->file->open() || pending->file->write(png) != png.size() || !pending->file->flush()) {
        pending->file.reset();
        oneDone();
        return;
    }
    runner.runAsync(QStringLiteral("ZXingReader"),
                    {QStringLiteral("-bytes"), QStringLiteral("-formats"), QStringLiteral("QRCode"), pending->file->fileName()}, {}, 10000,
                    this, [pending, oneDone](const ProcessResult &result) {
                        if (result.started && result.finished)
                            for (const QString &line : QString::fromUtf8(result.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                                if (!line.trimmed().isEmpty())
                                    pending->codes << line.trimmed();
                        pending->file.reset();
                        oneDone();
                    });
}

namespace {
bool isSingleUrl(const QString &text) {
    static const QRegularExpression url(QStringLiteral("^https?://\\S+$"), QRegularExpression::CaseInsensitiveOption);
    return url.match(text).hasMatch();
}
} // namespace

void TextCapture::onRecognized(const ProcessResult &result, const QStringList &codes, TextLayout layout) {
    if (!codes.isEmpty()) {
        deliver(codes.join(QLatin1Char('\n')), true);
        return;
    }
    if (!result.succeeded()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not read the text"),
                                            result.describe(QStringLiteral("tesseract")).simplified());
        emit finished({});
        return;
    }
    if (layout == TextLayout::Table) {
        const QString table = tableFromTsv(result.out);
        if (!table.isEmpty())
            deliver(table, false);
        else // fewer than two columns: copy Lines instead, from the same word boxes
            deliver(linesFromTsv(result.out), false, QStringLiteral("No table found, copied lines"));
        return;
    }
    if (layout == TextLayout::Lines)
        deliver(linesFromTsv(result.out), false);
    else
        deliver(joinParagraphs(QString::fromUtf8(result.out)), false);
}

void TextCapture::deliver(const QString &text, bool fromCodes, const QString &notice) {
    auto &notifier = m_daemon.service<Notifier>();
    if (text.isEmpty()) {
        notifier.notify(QStringLiteral("No text found"));
        emit finished({});
        return;
    }
    QString error;
    if (!m_daemon.service<Clipboard>().copyText(text, &error)) {
        notifier.notify(QStringLiteral("Tarn could not copy the text"), error.simplified());
        emit finished({});
        return;
    }
    // Tarn only ever announces a link; it never opens one.
    if (isSingleUrl(text))
        notifier.notify(QStringLiteral("Link copied"), text);
    else if (fromCodes)
        notifier.notify(QStringLiteral("QR code copied"), text);
    else if (!notice.isEmpty())
        notifier.notify(notice);
    else
        notifier.notify(text.section(QLatin1Char('\n'), 0, 0));
    emit finished(text);
}

// `capture text` (Ctrl+Shift+Print): the Overlay in text mode, area only.
TARN_MODULE(capture_text, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("capture text"),
                      [&daemon](const QStringList &) { return daemon.service<Overlay>().open(OverlayModel::Text); });
});

} // namespace tarn
