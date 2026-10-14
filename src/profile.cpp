#include "profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

QString Profile::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/bindings.json");
}

bool Profile::load(const QString &path, QString *error)
{
    m_path = path;
    m_devices.clear();

    QFile f(path);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    remappingEnabled = root.value(QStringLiteral("remappingEnabled")).toBool(true);
    const QJsonObject devices = root.value(QStringLiteral("devices")).toObject();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        const QJsonObject obj = it.value().toObject();
        DeviceProfile dp;
        dp.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        const QJsonObject bindings = obj.value(QStringLiteral("bindings")).toObject();
        for (auto b = bindings.begin(); b != bindings.end(); ++b) {
            const int source = Keys::fromName(b.key());
            if (source < 0)
                continue;
            QList<int> targets;
            bool valid = true;
            for (const QJsonValue &v : b.value().toArray()) {
                const int code = Keys::fromName(v.toString());
                valid = valid && Keys::isValidTarget(code);
                targets << code;
            }
            if (valid)
                dp.bindings.insert(source, targets);
        }
        m_devices.insert(it.key(), dp);
    }
    return true;
}

bool Profile::save(QString *error) const
{
    QJsonObject devices;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        QList<int> sources = it->bindings.keys();
        std::sort(sources.begin(), sources.end());
        QJsonObject bindings;
        for (int source : sources) {
            QJsonArray targets;
            for (int t : it->bindings.value(source))
                targets.append(Keys::name(t));
            bindings.insert(Keys::name(source), targets);
        }
        devices.insert(it.key(), QJsonObject{{QStringLiteral("enabled"), it->enabled},
                                             {QStringLiteral("bindings"), bindings}});
    }
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("remappingEnabled"), remappingEnabled},
                           {QStringLiteral("devices"), devices}};

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
