#include "daemon.h"

#include <QDir>
#include <algorithm>

namespace tarn {

static QList<std::pair<QString, ModuleRegistration::Init>> &registry() {
    static QList<std::pair<QString, ModuleRegistration::Init>> list;
    return list;
}

ModuleRegistration::ModuleRegistration(const char *name, Init init) {
    registry().append({QString::fromLatin1(name), init});
}

const QList<std::pair<QString, ModuleRegistration::Init>> &ModuleRegistration::all() {
    auto &list = registry();
    std::sort(list.begin(), list.end(), [](auto &a, auto &b) { return a.first < b.first; });
    return list;
}

Daemon::Daemon(Clock clock, QObject *parent) : QObject(parent), m_clock(std::move(clock)) {
    for (const auto &entry : ModuleRegistration::all())
        entry.second(*this);
}

Daemon::~Daemon() = default;

void Daemon::addCommand(const QString &words, Command command) {
    m_commands.insert(words, std::move(command));
}

Reply Daemon::handle(const QStringList &request) {
    for (int n = request.size(); n > 0; --n) {
        const auto it = m_commands.constFind(request.mid(0, n).join(' '));
        if (it != m_commands.constEnd())
            return (*it)(request.mid(n));
    }
    return Reply::failure(request.isEmpty() ? QStringLiteral("empty request")
                                            : QStringLiteral("unknown command: %1").arg(request.join(' ')));
}

QString Daemon::runtimeDir() {
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty())
        base = QDir::tempPath() + QStringLiteral("/tarn-run-") + qEnvironmentVariable("USER");
    return base + QStringLiteral("/tarn");
}

QString Daemon::controlSocketPath() {
    return runtimeDir() + QStringLiteral("/tarn.sock");
}

} // namespace tarn
