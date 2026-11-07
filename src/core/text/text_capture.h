#pragma once
// Text capture (Ctrl+Shift+Print): Tesseract reads the Selection, the text goes
// to the clipboard, and one notification shows its first line (or "No text
// found"). ZXingReader reads QR codes in the same picture; their contents (one per
// line) replace the text ("QR code copied"). A result that is exactly one URL is
// announced as "Link copied". Tarn never opens a link. It never writes History.
// Tesseract reads the Selection enlarged 2x (less for a large one, to stay under
// 8 megapixels): screen text is smaller than it is tuned for. ZXingReader reads
// it as it is.
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include "text_layout.h"

namespace tarn {

class Daemon;

// Tesseract prints one line per text line and a blank line between blocks.
// Lines within a paragraph are joined with a space; blank lines between
// paragraphs are kept (as exactly one). Empty when there is no text.
QString joinParagraphs(const QString &recognized);

class TextCapture : public QObject {
    Q_OBJECT
public:
    explicit TextCapture(Daemon &daemon);
    ~TextCapture() override;

    // Reads `selection` (a cut-out of the frozen image), copies the text and
    // notifies. Tesseract runs in the background (it can take seconds on a big
    // area), so this returns at once; finished() says when it is all done.
    void capture(const QImage &selection);
    // True while a read is running.
    bool busy() const { return m_running > 0; }

signals:
    // The copied text; empty when nothing was found or on failure (already notified).
    void finished(const QString &text);

private:
    // The first read is done (Tesseract, and ZXingReader beside it). QR codes
    // win over the text, whatever the layout.
    void onRecognized(const struct ProcessResult &result, const QStringList &codes, TextLayout layout);
    // The Selection is cut out and encoded: run Tesseract on `enlargedPng`
    // and ZXingReader on `png` side by side.
    void read(const QByteArray &png, const QByteArray &enlargedPng, const QStringList &args, TextLayout layout);
    void couldNotCutOut();
    void deliver(const QString &text, bool fromCodes, const QString &notice = {});
    Daemon &m_daemon;
    int m_running = 0;
    QThreadPool m_pool; // scales and encodes the Selection off the daemon's thread
};

} // namespace tarn
