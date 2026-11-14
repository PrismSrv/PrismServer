// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <launch/LaunchStep.h>

#include "java/JavaMetadata.h"
#include "meta/Version.h"
#include "tasks/Task.h"

class DownloadServerJava : public LaunchStep {
    Q_OBJECT

   public:
    explicit DownloadServerJava(LaunchTask* parent, int requiredMajor);
    void executeTask() override;
    bool canAbort() const override { return m_current ? m_current->canAbort() : false; }
    bool abort() override;

   private:
    void onIndexLoaded();
    void installRuntime();
    void acceptJava(const QString& path);
    void track(Task::Ptr task);

    int m_requiredMajor = 0;
    QString m_arch;
    Task::Ptr m_current;
    Meta::Version::Ptr m_version;
    Java::MetadataPtr m_runtime;
};
