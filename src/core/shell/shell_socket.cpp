#include "shell_socket.h"

#include "daemon/daemon.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

namespace tarn {

QString ShellSocket::socketPath() {
    return Daemon::runtimeDir() + QStringLiteral("/shell.sock");
}

QByteArray ShellSocket::line(const RecordingSession &session) {
    QString name;
    switch (session.state()) {
    case RecordingSession::Idle: name = QStringLiteral("idle"); break;
    case RecordingSession::Starting: name = QStringLiteral("starting"); break;
    case RecordingSession::Recording: name = QStringLiteral("recording"); break;
    case RecordingSession::Paused: name = QStringLiteral("paused"); break;
    case RecordingSession::Encoding: name = QStringLiteral("encoding"); break;
    case RecordingSession::Done: name = QStringLiteral("done"); break;
    case RecordingSession::Deleted: name = QStringLiteral("deleted"); break;
    }
    QJsonObject o{{"recording", name}};
    if (session.state() != RecordingSession::Idle) {
        o["elapsedMs"] = double(session.elapsedMs());
        o["format"] = session.format() == RecordingSession::Gif ? QStringLiteral("gif") : QStringLiteral("mp4");
    }
    if (session.state() == RecordingSession::Encoding)
        o["progress"] = session.encodingProgress();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

ShellSocket::ShellSocket(Daemon &daemon) : QObject(&daemon), m_session(daemon.service<RecordingSession>()) {
    connect(&m_session, &RecordingSession::stateChanged, this, &ShellSocket::onStateChanged);
    connect(&m_session, &RecordingSession::encodingProgressChanged, this, &ShellSocket::onProgress);

    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            m_clients << socket;
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                m_clients.removeAll(socket);
                socket->deleteLater();
            });
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
            socket->write(line(m_session) + '\n'); // the current state, at once
            socket->flush();
        }
    });

    // The socket is private to the user, like the control socket. A failure here only
    // means no shell can show the Recording; Tarn goes on as if it were not running.
    const QString path = socketPath();
    QDir().mkpath(Daemon::runtimeDir());
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(path); // a socket file left by a crashed daemon
    if (!m_server.listen(path))
        qWarning("tarn: Shell socket %s: %s", qPrintable(path), qPrintable(m_server.errorString()));
}

ShellSocket::~ShellSocket() {
    // The client sockets are children of m_server, which dies after m_clients: stop their
    // lambdas (which touch m_clients) from running while the members go.
    for (QLocalSocket *client : std::as_const(m_clients))
        client->disconnect(this);
    m_clients.clear();
}

void ShellSocket::onStateChanged() {
    m_lastProgress = m_session.state() == RecordingSession::Encoding ? m_session.encodingProgress() : -1;
    broadcast(line(m_session));
}

void ShellSocket::onProgress(double progress) {
    // The session emits 0 right after entering Encoding, which the state line already carried.
    if (m_session.state() != RecordingSession::Encoding || progress == m_lastProgress)
        return;
    m_lastProgress = progress;
    broadcast(line(m_session));
}

void ShellSocket::broadcast(const QByteArray &line) {
    // A write to a client that has just gone can emit `disconnected`, which edits m_clients: walk a copy.
    const QList<QLocalSocket *> clients = m_clients;
    for (QLocalSocket *client : clients) {
        if (!m_clients.contains(client) || client->state() != QLocalSocket::ConnectedState)
            continue;
        client->write(line + '\n');
        client->flush();
    }
}

void ShellSocket::onReadyRead(QLocalSocket *socket) {
    while (socket->canReadLine()) {
        const auto doc = QJsonDocument::fromJson(socket->readLine());
        if (!doc.isObject() || doc.object().value(QStringLiteral("stop")).toBool() != true)
            continue; // unknown or malformed: ignored
        // Same as the record key, but only while recording or paused.
        if (m_session.state() == RecordingSession::Recording || m_session.state() == RecordingSession::Paused)
            m_session.stop();
    }
}

TARN_MODULE(shell, [](Daemon &daemon) { daemon.service<ShellSocket>(); });

} // namespace tarn
