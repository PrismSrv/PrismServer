// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QCryptographicHash>
#include <QUrl>

#include "InstanceTask.h"
#include "net/NetJob.h"

#include <memory>

class PaperServerCreationTask final : public InstanceTask {
    Q_OBJECT
   public:
    PaperServerCreationTask(QString software,
                            QString softwareName,
                            QString version,
                            int build,
                            QUrl downloadUrl,
                            QString checksum,
                            QCryptographicHash::Algorithm checksumAlgorithm);

    void executeTask() override;
    bool abort() override;

   private:
    void startDownload();
    void downloadFinished();

    std::unique_ptr<MinecraftInstance> m_instance;
    NetJob::Ptr m_job;

    QString m_software;
    QString m_softwareName;
    QString m_version;
    int m_build = 0;
    QUrl m_downloadUrl;
    QString m_checksum;
    QCryptographicHash::Algorithm m_checksumAlgorithm = QCryptographicHash::Sha256;
};
