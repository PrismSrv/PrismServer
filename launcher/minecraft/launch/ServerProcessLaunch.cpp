// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "ServerProcessLaunch.h"

#include "Commandline.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"

#include <QFileInfo>
#include <algorithm>

ServerProcessLaunch::ServerProcessLaunch(LaunchTask* parent) : LaunchStep(parent), m_process(QStringConverter::Utf8)
{
    connect(&m_process, &LoggedProcess::log, this, &ServerProcessLaunch::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &ServerProcessLaunch::onStateChanged);
}

void ServerProcessLaunch::executeTask()
{
    auto* instance = m_parent->instance();
    auto* settings = instance->settings();
    const auto jarPath = instance->serverJarPath();
    if (!QFileInfo::exists(jarPath)) {
        const auto reason = tr("Server jar was not found:\n  %1").arg(jarPath);
        emit logLine(reason, MessageLevel::Fatal);
        emitFailed(reason);
        return;
    }

    const auto javaPath = FS::ResolveExecutable(settings->get("JavaPath").toString());
    if (javaPath.isEmpty()) {
        const auto reason = tr("Java path is not valid. Set Java in Settings, then start the server again.");
        emit logLine(reason, MessageLevel::Fatal);
        emitFailed(reason);
        return;
    }

    int minMemory = settings->get("MinMemAlloc").toInt();
    int maxMemory = settings->get("MaxMemAlloc").toInt();
    if (minMemory > maxMemory)
        std::swap(minMemory, maxMemory);

    QStringList args;
    if (minMemory > 0 && maxMemory > 0) {
        args << QString("-Xms%1m").arg(minMemory);
        args << QString("-Xmx%1m").arg(maxMemory);
    }
    args << Commandline::splitArgs(settings->get("JvmArgs").toString());
    args << "-jar" << jarPath << "nogui";

    const auto workDir = m_workingDirectory.isEmpty() ? instance->gameRoot() : m_workingDirectory;
    m_process.setWorkingDirectory(workDir);
    m_process.setProcessEnvironment(instance->createEnvironment());

    emit logLine(tr("Starting server:\n  %1 %2\n  in %3\n").arg(javaPath, args.join(' '), workDir), MessageLevel::Launcher);
    m_process.start(javaPath, args);
}

bool ServerProcessLaunch::sendCommand(const QString& command)
{
    if (command.trimmed().isEmpty())
        return false;
    if (m_process.state() != LoggedProcess::Running) {
        emit logLine(tr("The server is not running yet.\n"), MessageLevel::Error);
        return false;
    }

    const auto payload = QString(command + QLatin1Char('\n')).toUtf8();
    if (m_process.write(payload) == -1) {
        emit logLine(tr("Could not send the command.\n"), MessageLevel::Error);
        return false;
    }

    emit logLine(QStringLiteral("> %1").arg(command), MessageLevel::Launcher);
    return true;
}

void ServerProcessLaunch::onStateChanged(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::FailedToStart:
            emitFailed(tr("Could not start the server: %1").arg(m_process.errorString()));
            return;
        case LoggedProcess::Aborted:
            m_parent->setPid(-1);
            m_parent->instance()->setMinecraftRunning(false);
            emitFailed(tr("Server killed."));
            return;
        case LoggedProcess::Crashed:
            m_parent->setPid(-1);
            m_parent->instance()->setMinecraftRunning(false);
            emitFailed(tr("Server crashed."));
            return;
        case LoggedProcess::Finished: {
            m_parent->setPid(-1);
            m_parent->instance()->setMinecraftRunning(false);
            const auto exitCode = m_process.exitCode();
            if (exitCode != 0) {
                emitFailed(tr("Server exited with code %1.").arg(exitCode));
                return;
            }
            emitSucceeded();
            return;
        }
        case LoggedProcess::Running:
            m_parent->setPid(m_process.processId());
            m_parent->instance()->setMinecraftRunning(true);
            emit logLine(tr("Server process ID: %1\n").arg(m_process.processId()), MessageLevel::Launcher);
            return;
        default:
            return;
    }
}

bool ServerProcessLaunch::abort()
{
    const auto state = m_process.state();
    if (state == LoggedProcess::NotRunning || state == LoggedProcess::Finished || state == LoggedProcess::FailedToStart ||
        state == LoggedProcess::Crashed || state == LoggedProcess::Aborted) {
        return true;
    }

    if (state == LoggedProcess::Running && !m_stopSent) {
        m_stopSent = true;
        if (m_process.write("stop\n") == -1) {
            emit logLine(tr("Could not send the stop command. Killing the process.\n"), MessageLevel::Error);
            m_process.kill();
            return true;
        }
        emit logLine(tr("Sent the stop command.\n"), MessageLevel::Launcher);
        return false;
    }

    m_process.kill();
    return true;
}
