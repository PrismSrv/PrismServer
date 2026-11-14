// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <LoggedProcess.h>
#include <launch/LaunchStep.h>

class ServerProcessLaunch : public LaunchStep {
    Q_OBJECT
   public:
    explicit ServerProcessLaunch(LaunchTask* parent);
    void executeTask() override;
    bool sendCommand(const QString& command);
    bool abort() override;
    bool canAbort() const override { return true; }
    void setWorkingDirectory(const QString& workingDirectory) { m_workingDirectory = workingDirectory; }

   private slots:
    void onStateChanged(LoggedProcess::State state);

   private:
    LoggedProcess m_process;
    QString m_workingDirectory;
    bool m_stopSent = false;
};
