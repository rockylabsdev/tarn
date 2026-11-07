#include "control_server.h"

#include "daemon.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

namespace tarn {

ControlServer::ControlServer(Daemon &daemon, QObject *parent) : QObject(parent), m_daemon(daemon) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                while (socket->canReadLine()) {
                    const auto doc = QJsonDocument::fromJson(socket->readLine());
                    QStringList args;
                    for (const auto &v : doc.object().value("args").toArray())
                        args << v.toString();
                    const Reply reply = m_daemon.handle(args);
                    QJsonObject out{{"ok", reply.ok}};
                    if (!reply.ok)
                        out["error"] = reply.error;
                    socket->write(QJsonDocument(out).toJson(QJsonDocument::Compact) + '\n');
                    socket->flush();
                }
            });
        }
    });
}

QString ControlServer::listen(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Clears a socket file left by a crashed daemon; the caller has already
    // checked that no live daemon answers there.
    QLocalServer::removeServer(path);
    if (!m_server.listen(path))
        return m_server.errorString();
    return {};
}

} // namespace tarn
