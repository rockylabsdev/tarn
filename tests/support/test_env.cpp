#include "test_env.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>

namespace tarn::test {

void TestEnv::setEnv(const char *name, const QString &value) {
    if (!m_savedEnv.contains(name))
        m_savedEnv.insert(name, qEnvironmentVariableIsSet(name) ? qgetenv(name) : QByteArray());
    qputenv(name, value.toUtf8());
}

TestEnv::TestEnv() : m_dir(QDir::tempPath() + "/tarn-test-XXXXXX") {
    QVERIFY2(m_dir.isValid(), "could not create the scratch directory");
    for (const char *sub : {"home", "run", "config", "data", "state", "cache", "sysbin", "stubs"})
        QVERIFY(QDir().mkpath(scratch() + '/' + sub));
    QVERIFY(QFile::setPermissions(runtimeDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    setEnv("HOME", home());
    setEnv("XDG_CONFIG_HOME", scratch() + "/config");
    setEnv("XDG_DATA_HOME", scratch() + "/data");
    setEnv("XDG_STATE_HOME", scratch() + "/state");
    setEnv("XDG_CACHE_HOME", scratch() + "/cache");
    setEnv("XDG_RUNTIME_DIR", runtimeDir());
    for (const char *unwanted : {"XDG_DATA_DIRS", "XDG_CONFIG_DIRS"})
        setEnv(unwanted, QString());
    // Never let a test reach a real compositor.
    for (const char *name : {"WAYLAND_DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE", "DISPLAY"})
        setEnv(name, QString());

    // The XDG user folders: Pictures is inside the scratch home.
    QFile dirs(scratch() + "/config/user-dirs.dirs");
    QVERIFY(dirs.open(QIODevice::WriteOnly));
    dirs.write("XDG_PICTURES_DIR=\"$HOME/Pictures\"\nXDG_VIDEOS_DIR=\"$HOME/Videos\"\n");
    dirs.close();

    // PATH: stubs first, then symlinks to the few real tools they need.
    QFile tools(TARN_SAFE_TOOLS_FILE);
    QVERIFY(tools.open(QIODevice::ReadOnly));
    for (const QByteArray &line : tools.readAll().split('\n')) {
        const QString tool = QString::fromUtf8(line).trimmed();
        if (tool.isEmpty() || tool.startsWith('#'))
            continue;
        const QString real = QStandardPaths::findExecutable(tool, {"/usr/bin", "/bin"});
        QVERIFY2(!real.isEmpty(), qPrintable("safe tool not installed: " + tool));
        QVERIFY(QFile::link(real, scratch() + "/sysbin/" + tool));
    }
    setEnv("PATH", QStringLiteral(TARN_STUB_DIR) + ':' + scratch() + "/sysbin");
    setEnv("TARN_STUB_STATE", stateDir());

    m_now = QDateTime(QDate(2026, 11, 3), QTime(9, 30, 15));
    restartDaemon();
}

TestEnv::~TestEnv() {
    m_daemon.reset();
    for (auto it = m_savedEnv.cbegin(); it != m_savedEnv.cend(); ++it) {
        if (it.value().isNull())
            qunsetenv(it.key().constData());
        else
            qputenv(it.key().constData(), it.value());
    }
}

void TestEnv::restartDaemon() {
    m_daemon.reset();
    m_daemon = std::make_unique<Daemon>([this] { return m_now; });
}

QList<QStringList> TestEnv::calls(const QString &stub) const {
    QList<QStringList> out;
    QFile log(stateDir() + "/calls/" + stub + ".log");
    if (!log.open(QIODevice::ReadOnly))
        return out;
    QByteArray all = log.readAll();
    if (all.endsWith('\n'))
        all.chop(1);
    if (all.isEmpty())
        return out;
    // One line per call. A call with no arguments is an empty line.
    for (const QByteArray &line : all.split('\n')) {
        QStringList args;
        if (!line.isEmpty())
            for (const QByteArray &a : line.split('\x1f'))
                args << QString::fromUtf8(a);
        out << args;
    }
    return out;
}

QByteArray TestEnv::stdinOfCall(const QString &stub, int n) const {
    return readFile(stateDir() + "/stdin/" + stub + '.' + QString::number(n));
}

void TestEnv::setMode(const QString &stub, const QString &mode) {
    QDir().mkpath(stateDir() + "/mode");
    QFile f(stateDir() + "/mode/" + stub);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(mode.toUtf8());
}

void TestEnv::setFixture(const QString &stub, const QString &file, const QByteArray &content) {
    QDir().mkpath(stateDir() + "/fixtures/" + stub);
    QFile f(stateDir() + "/fixtures/" + stub + '/' + file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(content);
}

void TestEnv::setPointer(int x, int y) {
    setFixture("hyprctl", "cursorpos.json", QStringLiteral("{\"x\": %1, \"y\": %2}\n").arg(x).arg(y).toUtf8());
}

QByteArray TestEnv::defaultGrimPng() {
    return readFile(QStringLiteral(TARN_STUB_DIR "/fixtures/grim/output.png"));
}

QStringList TestEnv::filesIn(const QString &dir) {
    return QDir(dir).entryList(QDir::Files, QDir::Name);
}

QByteArray TestEnv::readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace tarn::test
