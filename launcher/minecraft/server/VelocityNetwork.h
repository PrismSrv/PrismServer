// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QList>
#include <QString>

struct VelocityBackend {
    QString name;
    QString version;
    QString software;
    QString root;
    int port = 25565;
};

namespace VelocityNetwork {

QList<VelocityBackend> paperBackends();

// Backends named in velocity.toml, in try order, matched to Paper and Purpur instances.
[[nodiscard]] QList<VelocityBackend> configuredBackends(const QString& velocityRoot);

// Writes velocity.toml and forwarding.secret, and points each selected Paper server at this proxy.
[[nodiscard]] bool apply(const QString& velocityRoot, const QString& jarPath, QList<VelocityBackend> selected, QString* error);

// Replaces [servers] in an existing velocity.toml and points each selected backend at this proxy.
// Leaves the rest of the config and the existing forwarding secret in place.
[[nodiscard]] bool updateRouting(const QString& velocityRoot, QList<VelocityBackend> selected, QString* error);

// Keeps the proxy bind, online mode, and player count aligned with the first-start dialog.
[[nodiscard]] bool syncProxySettings(const QString& velocityRoot, int port, bool onlineMode, int maxPlayers);

}  // namespace VelocityNetwork
