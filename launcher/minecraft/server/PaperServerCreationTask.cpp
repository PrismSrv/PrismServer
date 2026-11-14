// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "PaperServerCreationTask.h"

#include "Application.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/server/PaperApi.h"
#include "minecraft/server/ServerFiles.h"
#include "minecraft/server/VelocityNetwork.h"
#include "ui/dialogs/VelocityNetworkDialog.h"
#include "net/ChecksumValidator.h"
#include "settings/INISettingsObject.h"

#include <QApplication>
#include <QCryptographicHash>

PaperServerCreationTask::PaperServerCreationTask(QString software,
                                                 QString softwareName,
                                                 QString version,
                                                 int build,
                                                 QUrl downloadUrl,
                                                 QString checksum,
                                                 QCryptographicHash::Algorithm checksumAlgorithm)
    : m_software(std::move(software))
    , m_softwareName(std::move(softwareName))
    , m_version(std::move(version))
    , m_build(build)
    , m_downloadUrl(std::move(downloadUrl))
    , m_checksum(std::move(checksum))
    , m_checksumAlgorithm(checksumAlgorithm)
{
    setAbortable(true);
}

void PaperServerCreationTask::executeTask()
{
    setStatus(tr("Creating %1 %2 server").arg(m_softwareName, m_version));

    m_instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
    {
        const SettingsObject::Lock lock(m_instance->settings());
        m_instance->setName(name());
        const auto softwareIcon = ServerFiles::iconKeyForSoftware(m_software);
        if ((m_instIcon.isEmpty() || m_instIcon == QLatin1String("default")) && !softwareIcon.isEmpty())
            m_instIcon = softwareIcon;
        m_instance->setIconKey(m_instIcon);
        m_instance->settings()->set("ServerSoftware", m_software);
        m_instance->settings()->set("ServerVersion", m_version);
        m_instance->settings()->set("ServerBuild", QString::number(m_build));
        m_instance->settings()->set("ServerJar", "server.jar");
        m_instance->settings()->set("OverrideConsole", true);
        m_instance->settings()->set("ShowConsole", true);

        auto* components = m_instance->getPackProfile();
        components->buildingFromScratch();
        components->saveNow();
    }

    const auto root = m_instance->gameRoot();
    if (!FS::ensureFolderPathExists(root) || !FS::ensureFolderPathExists(FS::PathCombine(root, "plugins"))) {
        emitFailed(tr("Could not create the server folder."));
        return;
    }

    if (!ServerFiles::writeEula(root, false) || !ServerFiles::writeProperties(root, {})) {
        emitFailed(tr("Could not write the server files."));
        return;
    }
    startDownload();
}

void PaperServerCreationTask::startDownload()
{
    setStatus(tr("Downloading %1 %2 build %3").arg(m_softwareName, m_version, QString::number(m_build)));

    const auto destination = FS::PathCombine(m_instance->gameRoot(), "server.jar");
    auto request = Net::Request::makeFile(m_downloadUrl, destination);
    PaperApi::addUserAgent(request);
    if (!m_checksum.isEmpty())
        request->addValidator(new Net::ChecksumValidator(m_checksumAlgorithm, m_checksum));

    m_job = makeShared<NetJob>(tr("Paper download"), APPLICATION->network());
    m_job->addNetAction(request);
    connect(m_job.get(), &Task::progress, this, &PaperServerCreationTask::setProgress);
    connect(m_job.get(), &Task::status, this, &PaperServerCreationTask::setStatus);
    connect(m_job.get(), &Task::succeeded, this, &PaperServerCreationTask::downloadFinished);
    connect(m_job.get(), &Task::failed, this, &PaperServerCreationTask::emitFailed);
    connect(m_job.get(), &Task::aborted, this, &PaperServerCreationTask::emitAborted);
    m_job->start();
}

void PaperServerCreationTask::downloadFinished()
{
    if (m_software == QLatin1String("velocity")) {
        VelocityNetworkDialog dialog(VelocityNetwork::paperBackends(), QApplication::activeWindow());
        QList<VelocityBackend> selected;
        if (dialog.exec() == QDialog::Accepted)
            selected = dialog.selected();
        QString error;
        if (!VelocityNetwork::apply(m_instance->gameRoot(), m_instance->serverJarPath(), selected, &error)) {
            emitFailed(error);
            return;
        }
    }
    emitSucceeded();
}

bool PaperServerCreationTask::abort()
{
    if (!canAbort())
        return false;
    if (m_job)
        return m_job->abort();
    return Task::abort();
}
