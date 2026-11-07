// The test stub of hyprctl answers the queries the way the nested bench saw Hyprland
// answer them (scripts/bench.sh), so code written against the stub works on the desktop.
#include "test_env.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTest>

using tarn::test::TestEnv;

class HyprctlStubTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    QJsonArray query(const QStringList &args) {
        QProcess p;
        p.start(QStringLiteral("hyprctl"), args);
        if (!p.waitForFinished(5000))
            return {};
        return QJsonDocument::fromJson(p.readAllStandardOutput()).array();
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    // Lua binds: the key and the modifier mask are readable, the dispatcher is named
    // "HL.Dispatcher(exec_cmd)", and `arg` is an opaque number, not the command line.
    void luaBindsAreAnsweredInTheShapeHyprlandReports() {
        const QJsonArray binds = query({"binds", "-j"});
        QVERIFY(!binds.isEmpty());
        const QJsonObject b = binds.first().toObject();
        QCOMPARE(b.value("key").toString(), QStringLiteral("F9"));
        QCOMPARE(b.value("modmask").toInt(), 64); // SUPER
        QCOMPARE(b.value("dispatcher").toString(), QStringLiteral("HL.Dispatcher(exec_cmd)"));
        QVERIFY(b.value("arg").isString());
        QVERIFY(!b.value("arg").toString().contains(QLatin1String("tarn")));
        QVERIFY(b.contains("non_consuming"));
        // A bind's description is reported as written; Tarn's file describes its binds "Tarn: ...".
        QVERIFY(b.value("description").toString().startsWith(QLatin1String("Tarn:")));
        QVERIFY(b.value("has_description").toBool());
    }

    // reserved is four integers in the order left, top, right, bottom.
    void monitorsReportReservedAsLeftTopRightBottom() {
        const QJsonArray monitors = query({"monitors", "-j"});
        QVERIFY(!monitors.isEmpty());
        const QJsonArray reserved = monitors.first().toObject().value("reserved").toArray();
        QCOMPARE(reserved.size(), 4);
        QCOMPARE(reserved.at(1).toInt(), 56); // top: the bar the stub reserves
    }
};

QTEST_MAIN(HyprctlStubTest)
#include "hyprctl_stub_test.moc"
