#pragma once
// The daemon core: owns all state, answers requests, hands out services.
// Tests drive this class directly (handle(), service<T>()); the control socket
// and the QML screens are thin layers on top of it.
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <functional>
#include <typeindex>
#include <unordered_map>

namespace tarn {

struct Reply {
    bool ok = true;
    QString error; // when !ok: one plain sentence for the user
    static Reply success() { return {}; }
    static Reply failure(const QString &why) { return {false, why}; }
};

class Daemon : public QObject {
    Q_OBJECT
public:
    using Command = std::function<Reply(const QStringList &args)>;
    using Clock = std::function<QDateTime()>;

    explicit Daemon(QObject *parent = nullptr) : Daemon(Clock(), parent) {}
    // The clock is in place before the modules start, so start-up work (the
    // History sweep) sees it.
    explicit Daemon(Clock clock, QObject *parent = nullptr);
    ~Daemon() override;

    // Registers a command by its leading words, e.g. "capture fullscreen". The
    // longest registered prefix of a request wins; the rest are its arguments.
    void addCommand(const QString &words, Command command);

    // Runs one request exactly as if it had come over the socket.
    Reply handle(const QStringList &request);

    // Services are created on first use: T(Daemon &) must exist. A module
    // never has to be listed anywhere for its services to be reachable.
    template<class T> T &service() {
        auto &slot = m_services[std::type_index(typeid(T))];
        if (!slot) {
            auto *created = new T(*this);
            created->setParent(this);
            slot = created;
        }
        return *static_cast<T *>(slot);
    }

    // The time captures are named by. Tests replace it.
    QDateTime now() const { return m_clock ? m_clock() : QDateTime::currentDateTime(); }
    void setClock(Clock clock) { m_clock = std::move(clock); }

    // $XDG_RUNTIME_DIR/tarn: the daemon's socket folder.
    static QString runtimeDir();
    static QString controlSocketPath();

private:
    QHash<QString, Command> m_commands;
    std::unordered_map<std::type_index, QObject *> m_services;
    Clock m_clock;
};

// A module registers itself once, at file scope, and the Daemon runs every
// registered init when it is built:
//     TARN_MODULE(capture, [](tarn::Daemon &d) { d.addCommand(...); });
// Nothing outside the module's own folder needs editing.
struct ModuleRegistration {
    using Init = void (*)(Daemon &);
    ModuleRegistration(const char *name, Init init);
    static const QList<std::pair<QString, Init>> &all();
};
#define TARN_MODULE(name, init) static ::tarn::ModuleRegistration tarn_module_##name(#name, init)

} // namespace tarn
