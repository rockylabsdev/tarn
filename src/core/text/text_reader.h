#pragma once
// The Text reader: reads each History original in the background so History can
// be searched by what was in the picture. It writes the Description's `text`
// (missing = not yet read, "" = read, nothing found). One read at a time, under
// `nice`, paused while a Recording runs. It catches up at daemon start on every
// Description without `text`; a failed read leaves `text` missing until the next
// start. Without tesseract it reads nothing. Originals only: edits never
// trigger a read, and a change of languages never re-reads.
#include <QObject>
#include <QSet>
#include <QStringList>

namespace tarn {

class Daemon;

class TextReader : public QObject {
    Q_OBJECT
public:
    explicit TextReader(Daemon &daemon);

    // A read is running.
    bool busy() const { return m_busy; }

private:
    void enqueue(const QString &id);
    void pump();

    Daemon &m_daemon;
    QStringList m_queue;
    QSet<QString> m_tried;
    bool m_busy = false;
};

} // namespace tarn
