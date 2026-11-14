// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "DownloadServerJava.h"

#include <QDir>
#include <QFileInfo>

#include "Application.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "MessageLevel.h"
#include "SysInfo.h"
#include "java/JavaUtils.h"
#include "java/download/ArchiveDownloadTask.h"
#include "java/download/ManifestDownloadTask.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "net/Mode.h"

namespace {

QString findJavaBinary(const QString& root)
{
    const auto preferConsole = [](const QString& path) {
        QFileInfo info(path);
        if (info.fileName().compare(QStringLiteral("javaw.exe"), Qt::CaseInsensitive) == 0) {
            const auto consoleJava = info.dir().filePath(QStringLiteral("java.exe"));
            if (QFileInfo::exists(consoleJava))
                return consoleJava;
        }
        return path;
    };

    const QStringList names = { QStringLiteral("java.exe"), QStringLiteral("javaw.exe"), QStringLiteral("java") };
    const auto checkBin = [&](const QString& bin) {
        for (const auto& name : names) {
            const auto path = FS::PathCombine(bin, name);
            if (QFileInfo::exists(path))
                return preferConsole(path);
        }
        return QString();
    };

    const auto direct = checkBin(FS::PathCombine(root, "bin"));
    if (!direct.isEmpty())
        return direct;

    const QDir dir(root);
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        const auto nested = checkBin(FS::PathCombine(entry.filePath(), "bin"));
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}

}  // namespace

DownloadServerJava::DownloadServerJava(LaunchTask* parent, int requiredMajor)
    : LaunchStep(parent), m_requiredMajor(requiredMajor), m_arch(SysInfo::getSupportedJavaArchitecture())
{}

void DownloadServerJava::executeTask()
{
    if (m_arch.isEmpty()) {
        emitFailed(tr("This system has no Java runtime PrismServer can download."));
        return;
    }

    emit logLine(tr("No installed Java meets the requirement (Java %1). Downloading one.").arg(m_requiredMajor), MessageLevel::Launcher);
    auto* list = APPLICATION->metadataIndex()->get("net.minecraft.java").get();
    track(list->getLoadTask());
    connect(m_current.get(), &Task::succeeded, this, &DownloadServerJava::onIndexLoaded);
    connect(m_current.get(), &Task::failed, this, &DownloadServerJava::emitFailed);
    if (!m_current->isRunning())
        m_current->start();
    emit progressReportingRequest();
}

void DownloadServerJava::onIndexLoaded()
{
    if (!isRunning())
        return;

    auto list = APPLICATION->metadataIndex()->get("net.minecraft.java");
    const auto id = QStringLiteral("java%1").arg(m_requiredMajor);
    if (!list->hasVersion(id)) {
        emitFailed(tr("No Java %1 runtime is published for download.").arg(m_requiredMajor));
        return;
    }

    m_version = list->getVersion(id);
    if (m_version->isLoaded()) {
        installRuntime();
        return;
    }

    track(m_version->loadTask(Net::Mode::Online));
    connect(m_current.get(), &Task::succeeded, this, &DownloadServerJava::installRuntime);
    connect(m_current.get(), &Task::failed, this, &DownloadServerJava::emitFailed);
    if (!m_current->isRunning())
        m_current->start();
}

void DownloadServerJava::installRuntime()
{
    if (!isRunning())
        return;
    if (!m_version || !m_version->data()) {
        emitFailed(tr("Java %1 metadata did not load.").arg(m_requiredMajor));
        return;
    }

    m_runtime.reset();
    for (const auto& runtime : m_version->data()->runtimes) {
        if (runtime->runtimeOS != m_arch || runtime->version.major() < m_requiredMajor)
            continue;
        if (!m_runtime || runtime->releaseTime > m_runtime->releaseTime)
            m_runtime = runtime;
    }
    if (!m_runtime) {
        emitFailed(tr("No Java %1 download is available for %2.").arg(m_requiredMajor).arg(m_arch));
        return;
    }

    const auto root = QDir(APPLICATION->javaPath()).absoluteFilePath(m_runtime->m_name);
    if (const auto existing = findJavaBinary(root); !existing.isEmpty()) {
        acceptJava(existing);
        return;
    }

    switch (m_runtime->downloadType) {
        case Java::DownloadType::Manifest:
            track(makeShared<Java::ManifestDownloadTask>(m_runtime->url, root, m_runtime->checksumType, m_runtime->checksumHash));
            break;
        case Java::DownloadType::Archive:
            track(makeShared<Java::ArchiveDownloadTask>(m_runtime->url, root, m_runtime->checksumType, m_runtime->checksumHash));
            break;
        case Java::DownloadType::Unknown:
            emitFailed(tr("Could not determine how to download Java %1.").arg(m_requiredMajor));
            return;
    }

    connect(m_current.get(), &Task::succeeded, this, [this, root] {
        const auto binary = findJavaBinary(root);
        if (binary.isEmpty()) {
            FS::deletePath(root);
            emitFailed(tr("Java %1 downloaded, but java.exe was not in the archive.").arg(m_requiredMajor));
            return;
        }
        acceptJava(binary);
    });
    connect(m_current.get(), &Task::failed, this, [this, root](const QString& reason) {
        FS::deletePath(root);
        emitFailed(reason);
    });
    connect(m_current.get(), &Task::aborted, this, [root] { FS::deletePath(root); });
    m_current->start();
}

void DownloadServerJava::acceptJava(const QString& path)
{
    auto* settings = m_parent->instance()->settings();
    settings->set("OverrideJavaLocation", true);
    settings->set("JavaPath", path);
    settings->set("AutomaticJava", true);
    emit logLine(tr("Using Java at:\n  %1").arg(path), MessageLevel::Launcher);
    emitSucceeded();
}

void DownloadServerJava::track(Task::Ptr task)
{
    m_current = std::move(task);
    connect(m_current.get(), &Task::progress, this, &DownloadServerJava::setProgress);
    connect(m_current.get(), &Task::stepProgress, this, &DownloadServerJava::propagateStepProgress);
    connect(m_current.get(), &Task::status, this, &DownloadServerJava::setStatus);
    connect(m_current.get(), &Task::details, this, &DownloadServerJava::setDetails);
}

bool DownloadServerJava::abort()
{
    if (m_current && m_current->canAbort()) {
        const auto status = m_current->abort();
        emitAborted();
        return status;
    }
    return Task::abort();
}
