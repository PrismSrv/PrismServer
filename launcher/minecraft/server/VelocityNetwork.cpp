// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "VelocityNetwork.h"

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "archive/ArchiveReader.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/server/ServerFiles.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {

QString tomlQuoted(QString value)
{
    value.replace('\\', QStringLiteral("\\\\"));
    value.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

int indentOf(const QString& line)
{
    int indent = 0;
    while (indent < line.size() && line.at(indent).isSpace())
        ++indent;
    return indent;
}

QString childPad(int indent)
{
    return QString(indent + 2, QLatin1Char(' '));
}

bool patchVelocityBlock(QStringList* lines, const QString& secret)
{
    int proxiesLine = -1;
    int proxiesIndent = 0;
    for (int i = 0; i < lines->size(); ++i) {
        const auto trimmed = lines->at(i).trimmed();
        if (trimmed.startsWith(QLatin1Char('#')) || trimmed.isEmpty())
            continue;
        if (trimmed.startsWith(QStringLiteral("proxies:"))) {
            proxiesLine = i;
            proxiesIndent = indentOf(lines->at(i));
            break;
        }
    }

    int velocityLine = -1;
    int velocityIndent = 0;
    const int searchFrom = proxiesLine >= 0 ? proxiesLine + 1 : 0;
    for (int i = searchFrom; i < lines->size(); ++i) {
        const auto trimmed = lines->at(i).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        const int indent = indentOf(lines->at(i));
        if (proxiesLine >= 0 && indent <= proxiesIndent)
            break;
        if (trimmed.startsWith(QStringLiteral("velocity:"))) {
            velocityLine = i;
            velocityIndent = indent;
            break;
        }
    }

    if (velocityLine < 0) {
        const int indent = proxiesLine >= 0 ? proxiesIndent + 2 : 0;
        const auto pad = QString(indent, QLatin1Char(' '));
        const auto child = childPad(indent);
        QStringList block = { pad + QStringLiteral("velocity:"), child + QStringLiteral("enabled: true"),
                              child + QStringLiteral("online-mode: true"), child + QStringLiteral("secret: '%1'").arg(secret) };
        if (proxiesLine >= 0) {
            for (int offset = 0; offset < block.size(); ++offset)
                lines->insert(proxiesLine + 1 + offset, block.at(offset));
        } else {
            if (!lines->isEmpty() && !lines->last().isEmpty())
                lines->append(QString());
            lines->append(QStringLiteral("proxies:"));
            lines->append(QStringLiteral("  velocity:"));
            lines->append(QStringLiteral("    enabled: true"));
            lines->append(QStringLiteral("    online-mode: true"));
            lines->append(QStringLiteral("    secret: '%1'").arg(secret));
        }
        return true;
    }

    const auto child = childPad(velocityIndent);
    bool sawEnabled = false;
    bool sawOnline = false;
    bool sawSecret = false;
    int insertAt = velocityLine + 1;
    for (int i = velocityLine + 1; i < lines->size(); ++i) {
        const auto trimmed = lines->at(i).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (indentOf(lines->at(i)) <= velocityIndent)
            break;
        const auto key = trimmed.section(':', 0, 0).trimmed();
        if (key == QLatin1String("enabled")) {
            (*lines)[i] = child + QStringLiteral("enabled: true");
            sawEnabled = true;
        } else if (key == QLatin1String("online-mode")) {
            (*lines)[i] = child + QStringLiteral("online-mode: true");
            sawOnline = true;
        } else if (key == QLatin1String("secret")) {
            (*lines)[i] = child + QStringLiteral("secret: '%1'").arg(secret);
            sawSecret = true;
        }
        insertAt = i + 1;
    }
    QStringList missing;
    if (!sawEnabled)
        missing.append(child + QStringLiteral("enabled: true"));
    if (!sawOnline)
        missing.append(child + QStringLiteral("online-mode: true"));
    if (!sawSecret)
        missing.append(child + QStringLiteral("secret: '%1'").arg(secret));
    for (int offset = 0; offset < missing.size(); ++offset)
        lines->insert(insertAt + offset, missing.at(offset));
    return true;
}

bool writePaperVelocitySecret(const QString& serverRoot, const QString& secret, QString* error)
{
    const auto globalPath = FS::PathCombine(serverRoot, "config/paper-global.yml");
    const auto legacyPath = FS::PathCombine(serverRoot, "paper.yml");
    QString path = QFileInfo::exists(globalPath) ? globalPath : legacyPath;
    QStringList lines;
    if (QFileInfo::exists(path)) {
        const auto data = FS::read(path);
        if (!data) {
            *error = QObject::tr("Could not read %1.").arg(path);
            return false;
        }
        lines = QString::fromUtf8(*data).split('\n');
    } else {
        path = globalPath;
        if (!FS::ensureFolderPathExists(FS::PathCombine(serverRoot, "config"))) {
            *error = QObject::tr("Could not create the Paper config folder.");
            return false;
        }
    }

    patchVelocityBlock(&lines, secret);
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    if (!FS::write(path, (lines.join('\n') + '\n').toUtf8())) {
        *error = QObject::tr("Could not write %1.").arg(path);
        return false;
    }
    return true;
}

void replaceTomlSection(QStringList* lines, const QString& header, const QStringList& replacement)
{
    int start = -1;
    for (int i = 0; i < lines->size(); ++i) {
        if (lines->at(i).trimmed() == header) {
            start = i;
            break;
        }
    }
    if (start < 0) {
        if (!lines->isEmpty() && !lines->last().isEmpty())
            lines->append(QString());
        lines->append(replacement);
        return;
    }

    int end = start + 1;
    while (end < lines->size() && !lines->at(end).trimmed().startsWith('['))
        ++end;
    for (int i = end - 1; i >= start; --i)
        lines->removeAt(i);
    for (int i = 0; i < replacement.size(); ++i)
        lines->insert(start + i, replacement.at(i));
}

QStringList quotedStrings(const QString& text)
{
    QStringList out;
    bool inQuote = false;
    bool escape = false;
    QString current;
    for (int i = 0; i < text.size(); ++i) {
        const auto ch = text.at(i);
        if (!inQuote) {
            if (ch == QLatin1Char('"'))
                inQuote = true;
            continue;
        }
        if (escape) {
            current.append(ch);
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            out.append(current);
            current.clear();
            inQuote = false;
            continue;
        }
        current.append(ch);
    }
    return out;
}

QStringList serversSection(const QList<VelocityBackend>& selected)
{
    QSet<QString> usedNames;
    QStringList names;
    QStringList servers;
    servers << QStringLiteral("[servers]");
    servers << QStringLiteral("# Connected by PrismServer. The first server in try is where players join.");
    for (const auto& backend : selected) {
        auto name = backend.name.trimmed().isEmpty() ? QStringLiteral("server") : backend.name.trimmed();
        auto unique = name;
        int suffix = 2;
        while (usedNames.contains(unique))
            unique = QStringLiteral("%1 %2").arg(name).arg(suffix++);
        usedNames.insert(unique);
        names.append(unique);
        servers << QStringLiteral("%1 = \"127.0.0.1:%2\"").arg(tomlQuoted(unique)).arg(backend.port);
    }
    servers << QString();
    servers << QStringLiteral("try = [");
    for (int i = 0; i < names.size(); ++i)
        servers << QStringLiteral("    %1%2").arg(tomlQuoted(names.at(i)), i + 1 == names.size() ? QString() : QStringLiteral(","));
    servers << QStringLiteral("]");
    servers << QString();
    return servers;
}

bool assignBackendPorts(QList<VelocityBackend>* selected, QString* error)
{
    for (auto& backend : *selected)
        backend.port = ServerFiles::readProperties(backend.root).port;

    QSet<int> usedPorts;
    usedPorts.insert(25565);
    const auto bumpPort = [&usedPorts](int port) {
        if (port > 0 && !usedPorts.contains(port))
            return port;
        int next = 25566;
        while (usedPorts.contains(next))
            ++next;
        return next;
    };
    for (auto backend : VelocityNetwork::paperBackends()) {
        const bool chosen = std::any_of(selected->cbegin(), selected->cend(), [&backend](const VelocityBackend& item) {
            return item.root == backend.root;
        });
        if (chosen)
            continue;
        const int port = bumpPort(backend.port);
        if (port == backend.port) {
            usedPorts.insert(port);
            continue;
        }
        auto properties = ServerFiles::readProperties(backend.root);
        properties.port = port;
        if (!ServerFiles::writeProperties(backend.root, properties)) {
            *error = QObject::tr("Could not update server.properties for %1.").arg(backend.name);
            return false;
        }
        usedPorts.insert(port);
    }
    for (auto& backend : *selected) {
        const int port = bumpPort(backend.port);
        usedPorts.insert(port);
        backend.port = port;

        auto properties = ServerFiles::readProperties(backend.root);
        properties.port = port;
        properties.onlineMode = false;
        if (!ServerFiles::writeProperties(backend.root, properties)) {
            *error = QObject::tr("Could not update server.properties for %1.").arg(backend.name);
            return false;
        }
    }
    return true;
}

QString patchedToml(QString toml, const QList<VelocityBackend>& selected)
{
    auto lines = toml.split('\n');
    for (auto& line : lines) {
        if (line.startsWith(' ') || line.startsWith('\t') || line.trimmed().startsWith('#'))
            continue;
        if (line.trimmed().startsWith(QStringLiteral("player-info-forwarding-mode")))
            line = QStringLiteral("player-info-forwarding-mode = \"modern\"");
    }

    replaceTomlSection(&lines, QStringLiteral("[servers]"), serversSection(selected));

    // The stock config points these hostnames at sample servers. Those servers are gone, so Velocity refuses to boot.
    replaceTomlSection(&lines, QStringLiteral("[forced-hosts]"),
                       { QStringLiteral("[forced-hosts]"),
                         QStringLiteral("# Hostnames that send players straight to a backend. Leave this empty until you add one."),
                         QString() });
    return lines.join('\n');
}

}  // namespace

namespace VelocityNetwork {

QList<VelocityBackend> paperBackends()
{
    QList<VelocityBackend> backends;
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        if (!instance->isServer())
            continue;
        const auto software = instance->settings()->get(QStringLiteral("ServerSoftware")).toString();
        if (software != QLatin1String("paper") && software != QLatin1String("purpur"))
            continue;
        VelocityBackend backend;
        backend.name = instance->name();
        backend.version = instance->settings()->get(QStringLiteral("ServerVersion")).toString();
        backend.software = software;
        backend.root = instance->gameRoot();
        backend.port = ServerFiles::readProperties(backend.root).port;
        backends.append(backend);
    }
    return backends;
}

QList<VelocityBackend> configuredBackends(const QString& velocityRoot)
{
    const auto bytes = FS::read(FS::PathCombine(velocityRoot, "velocity.toml"));
    if (!bytes)
        return {};

    const auto lines = QString::fromUtf8(*bytes).split('\n');
    bool inServers = false;
    bool inTry = false;
    QHash<QString, int> portsByName;
    QStringList tryNames;
    for (const auto& line : lines) {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            inServers = trimmed == QLatin1String("[servers]");
            inTry = false;
            continue;
        }
        if (!inServers || trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (inTry) {
            auto chunk = trimmed;
            const int end = chunk.indexOf(QLatin1Char(']'));
            if (end >= 0) {
                chunk = chunk.left(end);
                inTry = false;
            }
            tryNames.append(quotedStrings(chunk));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("try"))) {
            auto rest = trimmed.section(QLatin1Char('='), 1);
            const int end = rest.indexOf(QLatin1Char(']'));
            if (end >= 0)
                rest = rest.left(end);
            else
                inTry = true;
            tryNames.append(quotedStrings(rest));
            continue;
        }
        const int eq = trimmed.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const auto left = quotedStrings(trimmed.left(eq));
        const auto right = quotedStrings(trimmed.mid(eq + 1));
        if (left.isEmpty() || right.isEmpty())
            continue;
        const auto address = right.first();
        const int colon = address.lastIndexOf(QLatin1Char(':'));
        portsByName.insert(left.first(), colon >= 0 ? address.mid(colon + 1).toInt() : 0);
    }

    const auto available = paperBackends();
    QList<VelocityBackend> result;
    QSet<QString> used;
    for (const auto& name : tryNames) {
        const VelocityBackend* match = nullptr;
        for (const auto& backend : available) {
            if (!used.contains(backend.root) && backend.name == name) {
                match = &backend;
                break;
            }
        }
        if (match == nullptr && portsByName.value(name) > 0) {
            const int port = portsByName.value(name);
            for (const auto& backend : available) {
                if (!used.contains(backend.root) && backend.port == port) {
                    match = &backend;
                    break;
                }
            }
        }
        if (match == nullptr)
            continue;
        result.append(*match);
        used.insert(match->root);
    }
    return result;
}

bool apply(const QString& velocityRoot, const QString& jarPath, QList<VelocityBackend> selected, QString* error)
{
    const auto tomlTemplate = MMCZip::ArchiveReader(jarPath).readFile(QStringLiteral("default-velocity.toml"));
    if (!tomlTemplate) {
        *error = QObject::tr("Could not read the default Velocity config from the server jar.");
        return false;
    }

    if (!assignBackendPorts(&selected, error))
        return false;

    const auto secret = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
    if (!FS::write(FS::PathCombine(velocityRoot, "forwarding.secret"), secret.toUtf8())) {
        *error = QObject::tr("Could not write the Velocity forwarding secret.");
        return false;
    }

    const auto toml = patchedToml(QString::fromUtf8(*tomlTemplate), selected);
    if (!FS::write(FS::PathCombine(velocityRoot, "velocity.toml"), toml.toUtf8())) {
        *error = QObject::tr("Could not write velocity.toml.");
        return false;
    }

    for (const auto& backend : selected) {
        if (!writePaperVelocitySecret(backend.root, secret, error))
            return false;
    }
    return true;
}

bool updateRouting(const QString& velocityRoot, QList<VelocityBackend> selected, QString* error)
{
    if (!assignBackendPorts(&selected, error))
        return false;

    const auto path = FS::PathCombine(velocityRoot, "velocity.toml");
    const auto bytes = FS::read(path);
    if (!bytes) {
        *error = QObject::tr("Could not read velocity.toml.");
        return false;
    }

    const auto text = QString::fromUtf8(*bytes);
    QString secretName = QStringLiteral("forwarding.secret");
    for (const auto& line : text.split('\n')) {
        if (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t')) || line.trimmed().startsWith(QLatin1Char('#')))
            continue;
        const auto trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral("forwarding-secret-file")))
            continue;
        const auto names = quotedStrings(trimmed.section(QLatin1Char('='), 1));
        if (!names.isEmpty() && !names.first().isEmpty())
            secretName = names.first();
        break;
    }

    const auto secretPath = FS::PathCombine(velocityRoot, secretName);
    QString secret;
    if (const auto secretBytes = FS::read(secretPath))
        secret = QString::fromUtf8(*secretBytes);
    if (secret.endsWith('\n'))
        secret.chop(1);
    if (secret.endsWith('\r'))
        secret.chop(1);
    if (secret.isEmpty()) {
        secret = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
        if (!FS::write(secretPath, secret.toUtf8())) {
            *error = QObject::tr("Could not write the Velocity forwarding secret.");
            return false;
        }
    }

    for (const auto& backend : selected) {
        if (!writePaperVelocitySecret(backend.root, secret, error))
            return false;
    }

    auto lines = text.split('\n');
    replaceTomlSection(&lines, QStringLiteral("[servers]"), serversSection(selected));
    if (!FS::write(path, lines.join('\n').toUtf8())) {
        *error = QObject::tr("Could not write velocity.toml.");
        return false;
    }
    return true;
}

bool syncProxySettings(const QString& velocityRoot, int port, bool onlineMode, int maxPlayers)
{
    const auto path = FS::PathCombine(velocityRoot, "velocity.toml");
    const auto data = FS::read(path);
    if (!data)
        return true;

    auto lines = QString::fromUtf8(*data).split('\n');
    for (auto& line : lines) {
        if (line.startsWith(' ') || line.startsWith('\t') || line.trimmed().startsWith('#'))
            continue;
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("bind")))
            line = QStringLiteral("bind = \"0.0.0.0:%1\"").arg(port);
        else if (trimmed.startsWith(QStringLiteral("online-mode")))
            line = QStringLiteral("online-mode = %1").arg(onlineMode ? "true" : "false");
        else if (trimmed.startsWith(QStringLiteral("show-max-players")))
            line = QStringLiteral("show-max-players = %1").arg(maxPlayers);
    }
    return bool(FS::write(path, lines.join('\n').toUtf8()));
}

}  // namespace VelocityNetwork
