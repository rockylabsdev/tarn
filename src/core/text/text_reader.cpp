#include "text_reader.h"

#include "daemon/daemon.h"
#include "history/history.h"
#include "process/process_runner.h"
#include "recording/recording_session.h"
#include "settings/settings.h"
#include "text/text_capture.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

namespace tarn {

TextReader::TextReader(Daemon &daemon) : m_daemon(daemon) {
    auto &history = daemon.service<History>();
    connect(&history, &History::entryAdded, this, &TextReader::enqueue);
    connect(&daemon.service<RecordingSession>(), &RecordingSession::stateChanged, this, [this] { pump(); });
    // Catch-up at start, once the daemon is up.
    QTimer::singleShot(0, this, [this] {
        auto &history = m_daemon.service<History>();
        for (const QString &id : history.ids()) {
            QFile file(history.descriptionPath(id));
            if (file.open(QIODevice::ReadOnly) && !QJsonDocument::fromJson(file.readAll()).object().contains(QStringLiteral("text")))
                enqueue(id);
        }
    });
}

void TextReader::enqueue(const QString &id) {
    if (m_tried.contains(id))
        return; // one try per entry per daemon run (a failed read waits for the next start)
    m_tried.insert(id);
    m_queue << id;
    pump();
}

void TextReader::pump() {
    if (m_busy || m_queue.isEmpty() || m_daemon.service<RecordingSession>().active())
        return;
    if (QStandardPaths::findExecutable(QStringLiteral("tesseract")).isEmpty())
        return; // nothing to read with, and nothing to say
    const QString id = m_queue.takeFirst();
    auto &history = m_daemon.service<History>();
    QFile png(history.pngPath(id));
    if (!png.open(QIODevice::ReadOnly)) {
        pump(); // gone (deleted meanwhile): skip it
        return;
    }
    m_busy = true;
    m_daemon.service<ProcessRunner>().runAsync(
        QStringLiteral("nice"),
        {QStringLiteral("-n"), QStringLiteral("19"), QStringLiteral("tesseract"), QStringLiteral("stdin"), QStringLiteral("stdout"),
         QStringLiteral("-l"), m_daemon.service<Settings>().textLanguages()},
        png.readAll(), 10 * 60 * 1000, this, [this, id](const ProcessResult &result) {
            m_busy = false;
            if (result.succeeded())
                m_daemon.service<History>().setText(id, joinParagraphs(QString::fromUtf8(result.out)));
            pump();
        });
}

TARN_MODULE(text_reader, [](Daemon &daemon) { daemon.service<TextReader>(); });

} // namespace tarn
