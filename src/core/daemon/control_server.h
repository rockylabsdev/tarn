#pragma once
// The control socket: one JSON line in, one JSON line out, per request.
//   -> {"args":["capture","fullscreen"]}
//   <- {"ok":true}   or   {"ok":false,"error":"..."}
#include <QLocalServer>
#include <QObject>

namespace tarn {

class Daemon;

class ControlServer : public QObject {
    Q_OBJECT
public:
    explicit ControlServer(Daemon &daemon, QObject *parent = nullptr);
    // Starts listening at path. Returns an empty string, or what went wrong.
    QString listen(const QString &path);

private:
    Daemon &m_daemon;
    QLocalServer m_server;
};

} // namespace tarn
