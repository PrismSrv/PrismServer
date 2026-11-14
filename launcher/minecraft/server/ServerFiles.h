// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace ServerFiles {

struct Properties {
    int port = 25565;
    bool onlineMode = true;
    int maxPlayers = 20;
};

struct PropertyValue {
    QString key;
    QString value;
};

struct PropertyMap {
    QStringList order;
    QHash<QString, QString> values;
};

[[nodiscard]] inline QString iconKeyForSoftware(const QString& software)
{
    if (software == QLatin1String("paper"))
        return QStringLiteral("papermclogo");
    if (software == QLatin1String("purpur"))
        return QStringLiteral("purpurlogo");
    if (software == QLatin1String("velocity"))
        return QStringLiteral("velocitylogo");
    return {};
}

[[nodiscard]] bool eulaAccepted(const QString& serverRoot);
[[nodiscard]] bool writeEula(const QString& serverRoot, bool accepted);
[[nodiscard]] Properties readProperties(const QString& serverRoot);
[[nodiscard]] bool writeProperties(const QString& serverRoot, const Properties& properties);
[[nodiscard]] PropertyMap readAll(const QString& serverRoot);
[[nodiscard]] bool writeValues(const QString& serverRoot, const QList<PropertyValue>& values);

}  // namespace ServerFiles
