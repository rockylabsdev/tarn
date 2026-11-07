#pragma once
// The Shell socket (docs/shell-socket.md, ADR 0002 and 0004): a Unix socket at
// $XDG_RUNTIME_DIR/tarn/shell.sock. A shell or bar connects; Tarn sends one JSON line on
// every Recording state change (and the current state on every new connection);
// the shell may send {"stop": true}. Unknown keys are ignored both ways. Tarn behaves
// the same whether or not anything is connected.
#include "recording/recording_session.h"

#include <QByteArray>
#include <QList>
#include <QLocalServer>
#include <QObject>

class QLocalSocket;

namespace tarn {

class Daemon;

class ShellSocket : public QObject {
    Q_OBJECT
public:
    explicit ShellSocket(Daemon &daemon);
    ~ShellSocket() override;

    static QString socketPath();
    // The line for `session`'s current state (without the newline).
    static QByteArray line(const RecordingSession &session);
    int clientCount() const { return m_clients.size(); }
    bool listening() const { return m_server.isListening(); }

private:
    void onStateChanged();
    void onProgress(double progress);
    void broadcast(const QByteArray &line);
    void onReadyRead(QLocalSocket *socket);

    RecordingSession &m_session;
    QLocalServer m_server;
    QList<QLocalSocket *> m_clients;
    double m_lastProgress = -1;
};

} // namespace tarn
