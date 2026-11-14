// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "ServerFiles.h"

#include "FileSystem.h"

#include <QSet>

namespace {

QString eulaPath(const QString& serverRoot)
{
    return FS::PathCombine(serverRoot, "eula.txt");
}

QString propertiesPath(const QString& serverRoot)
{
    return FS::PathCombine(serverRoot, "server.properties");
}

QString unescapeProperty(const QString& value)
{
    QString out;
    out.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        if (value.at(i) != '\\' || i + 1 >= value.size()) {
            out += value.at(i);
            continue;
        }
        const QChar next = value.at(++i);
        if (next == 'u' && i + 4 < value.size()) {
            bool ok = false;
            const uint code = value.mid(i + 1, 4).toUInt(&ok, 16);
            if (ok) {
                out += QChar(static_cast<ushort>(code));
                i += 4;
                continue;
            }
        }
        if (next == 'n')
            out += '\n';
        else if (next == 'r')
            out += '\r';
        else if (next == 't')
            out += '\t';
        else
            out += next;
    }
    return out;
}

QString escapeProperty(const QString& value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        switch (ch.unicode()) {
            case '\\':
                out += QStringLiteral("\\\\");
                break;
            case '\n':
                out += QStringLiteral("\\n");
                break;
            case '\r':
                out += QStringLiteral("\\r");
                break;
            case '\t':
                out += QStringLiteral("\\t");
                break;
            default:
                out += ch;
                break;
        }
    }
    return out;
}

bool parsePropertyLine(const QString& line, QString* key, QString* value)
{
    const auto trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#') || !trimmed.contains('='))
        return false;
    const int separator = trimmed.indexOf('=');
    *key = unescapeProperty(trimmed.left(separator).trimmed());
    *value = unescapeProperty(trimmed.mid(separator + 1).trimmed());
    return !key->isEmpty();
}

QStringList readLines(const QString& path)
{
    const auto data = FS::read(path);
    if (!data)
        return {};
    QStringList lines = QString::fromUtf8(*data).split('\n');
    for (auto& line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
    }
    return lines;
}

}  // namespace

namespace ServerFiles {

bool eulaAccepted(const QString& serverRoot)
{
    const auto data = FS::read(eulaPath(serverRoot));
    if (!data)
        return false;

    const auto lines = QString::fromUtf8(*data).split('\n');
    for (const auto& line : lines) {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith('#') || !trimmed.contains('='))
            continue;
        if (trimmed.section('=', 0, 0).trimmed().compare("eula", Qt::CaseInsensitive) == 0)
            return trimmed.section('=', 1).trimmed().compare("true", Qt::CaseInsensitive) == 0;
    }
    return false;
}

bool writeEula(const QString& serverRoot, bool accepted)
{
    const QString text = QStringLiteral(
                             "#By changing the setting below to TRUE you are indicating your agreement to our EULA "
                             "(https://aka.ms/MinecraftEULA).\n"
                             "eula=%1\n")
                             .arg(accepted ? "true" : "false");
    return bool(FS::write(eulaPath(serverRoot), text.toUtf8()));
}

Properties readProperties(const QString& serverRoot)
{
    Properties properties;
    const auto file = readAll(serverRoot);
    bool ok = false;
    const auto port = file.values.value(QStringLiteral("server-port")).toInt(&ok);
    if (ok)
        properties.port = port;
    const auto players = file.values.value(QStringLiteral("max-players")).toInt(&ok);
    if (ok)
        properties.maxPlayers = players;
    const auto online = file.values.value(QStringLiteral("online-mode"));
    if (!online.isEmpty())
        properties.onlineMode = online.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    return properties;
}

bool writeProperties(const QString& serverRoot, const Properties& properties)
{
    QList<PropertyValue> values = {
        { QStringLiteral("server-port"), QString::number(properties.port) },
        { QStringLiteral("online-mode"), properties.onlineMode ? QStringLiteral("true") : QStringLiteral("false") },
        { QStringLiteral("max-players"), QString::number(properties.maxPlayers) },
    };
    if (readAll(serverRoot).values.value(QStringLiteral("motd")).isEmpty())
        values.append({ QStringLiteral("motd"), QStringLiteral("A Minecraft Server") });
    return writeValues(serverRoot, values);
}

PropertyMap readAll(const QString& serverRoot)
{
    PropertyMap file;
    for (const auto& line : readLines(propertiesPath(serverRoot))) {
        QString key;
        QString value;
        if (!parsePropertyLine(line, &key, &value))
            continue;
        if (!file.values.contains(key))
            file.order.append(key);
        file.values.insert(key, value);
    }
    return file;
}

bool writeValues(const QString& serverRoot, const QList<PropertyValue>& values)
{
    if (values.isEmpty())
        return true;

    QHash<QString, QString> pending;
    QStringList order;
    for (const auto& entry : values) {
        if (!pending.contains(entry.key))
            order.append(entry.key);
        pending.insert(entry.key, entry.value);
    }

    const auto path = propertiesPath(serverRoot);
    auto lines = readLines(path);
    if (lines.isEmpty())
        lines.append(QStringLiteral("#Minecraft server properties"));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QSet<QString> written;
    for (auto& line : lines) {
        QString key;
        QString value;
        if (!parsePropertyLine(line, &key, &value) || !pending.contains(key))
            continue;
        line = key + QLatin1Char('=') + escapeProperty(pending.value(key));
        written.insert(key);
    }
    for (const auto& key : order) {
        if (written.contains(key))
            continue;
        lines.append(key + QLatin1Char('=') + escapeProperty(pending.value(key)));
    }
    return bool(FS::write(path, (lines.join('\n') + '\n').toUtf8()));
}

}  // namespace ServerFiles
