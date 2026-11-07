#pragma once
// Remembered state: what Tarn remembers from last use (each app's last area, and in
// later tickets the Magnifier, tool colors and sizes, Record panel toggles, the
// last Save-as folder). One machine-written file, $XDG_STATE_HOME/tarn/state.toml
// (default ~/.local/state/tarn/state.toml). Never mixed with Settings
// (config.toml) and always safe to delete: a missing or unreadable file is just
// "nothing remembered yet".
//
// The file is read and written with toml++ (flat `[section]` tables only).
//
// Adding a key is a local change: call value("section.name", fallback) and
// setValue("section.name", v) from your own module. Values are bool, int,
// double or string. "section.name" becomes `name = v` under `[section]`.
// Every setValue writes the file at once (atomically). setValueSoon is for a value
// that changes many times a second (a color slider being dragged): it is in use at
// once and written when it has settled for kSoonMs, or when Tarn quits.
#include <QMap>
#include <QObject>
#include <QRect>
#include <QTimer>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace tarn {

class Daemon;

class RememberedState : public QObject {
    Q_OBJECT
public:
    explicit RememberedState(Daemon &);
    explicit RememberedState(const QString &path, QObject *parent = nullptr);

    static QString defaultPath();
    QString path() const { return m_path; }

    bool contains(const QString &key) const { return m_values.contains(key); }
    QVariant value(const QString &key, const QVariant &fallback = {}) const { return m_values.value(key, fallback); }
    void setValue(const QString &key, const QVariant &v);
    void setValueSoon(const QString &key, const QVariant &v);
    ~RememberedState() override;
    static constexpr int kSoonMs = 300;
    void remove(const QString &key);

    // A rectangle stored as four keys: prefix.x, .y, .width, .height. Returns
    // a null QRect if any is missing.
    QRect rect(const QString &prefix) const;
    void setRect(const QString &prefix, const QRect &r);

    // A group of keys stored under one prefix: map("watermark") is every
    // "watermark.<name>" as name -> value. setMap sets prefix.<name> for each entry
    // of `values` in one write (or as setValueSoon when `soon`); other keys stay.
    QVariantMap map(const QString &prefix) const;
    void setMap(const QString &prefix, const QVariantMap &values, bool soon = false);

private:
    void load();
    void save() const;

    QString m_path;
    QMap<QString, QVariant> m_values; // sorted: the file is stable
    mutable QTimer m_soon;            // a setValueSoon waiting to be written
};

} // namespace tarn
