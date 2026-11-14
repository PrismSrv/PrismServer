// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "PaperPage.h"

#include "Application.h"
#include "Json.h"
#include "Version.h"
#include "minecraft/server/PaperApi.h"
#include "minecraft/server/ServerFiles.h"
#include "minecraft/server/PaperServerCreationTask.h"
#include "net/NetJob.h"
#include "ui/dialogs/NewInstanceDialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QUrl purpurProjectUrl()
{
    return QUrl(QStringLiteral("https://api.purpurmc.org/v2/purpur"));
}

QUrl purpurLatestUrl(const QString& version)
{
    const auto encoded = QString::fromUtf8(QUrl::toPercentEncoding(version));
    return QUrl(QStringLiteral("https://api.purpurmc.org/v2/purpur/%1/latest").arg(encoded));
}

QUrl purpurDownloadUrl(const QString& version, int build)
{
    const auto encoded = QString::fromUtf8(QUrl::toPercentEncoding(version));
    return QUrl(QStringLiteral("https://api.purpurmc.org/v2/purpur/%1/%2/download").arg(encoded, QString::number(build)));
}

}  // namespace

PaperPage::PaperPage(NewInstanceDialog* dialog, ServerSoftwareSpec spec, QWidget* parent)
    : QWidget(parent), m_dialog(dialog), m_spec(std::move(spec))
{
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(m_spec.blurb, this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_versions = new QListWidget(this);
    layout->addWidget(m_versions, 1);

    m_preReleases = new QCheckBox(tr("Show pre-releases"), this);
    layout->addWidget(m_preReleases);

    auto* statusRow = new QHBoxLayout;
    m_status = new QLabel(tr("Loading %1 versions...").arg(m_spec.name), this);
    m_status->setWordWrap(true);
    statusRow->addWidget(m_status, 1);
    auto* refresh = new QPushButton(tr("Refresh"), this);
    statusRow->addWidget(refresh);
    layout->addLayout(statusRow);

    connect(m_versions, &QListWidget::currentItemChanged, this, &PaperPage::onVersionChanged);
    connect(m_preReleases, &QCheckBox::toggled, this, &PaperPage::onPreReleasesToggled);
    connect(refresh, &QPushButton::clicked, this, &PaperPage::refreshVersions);
}

QIcon PaperPage::icon() const
{
    const auto key = ServerFiles::iconKeyForSoftware(m_spec.id);
    return QIcon::fromTheme(key.isEmpty() ? QStringLiteral("minecraft") : key);
}

PaperPage::~PaperPage()
{
    if (m_job) {
        disconnect(m_job.get(), nullptr, this, nullptr);
        m_job->abort();
    }
}

void PaperPage::openedImpl()
{
    m_dialog->setSuggestedIcon(ServerFiles::iconKeyForSoftware(m_spec.id));
    if (!m_loaded)
        refreshVersions();
}

void PaperPage::refreshVersions()
{
    const int generation = ++m_generation;
    if (m_job)
        m_job->abort();

    m_status->setText(tr("Loading %1 versions...").arg(m_spec.name));
    m_dialog->setSuggestedPack();

    auto [request, body] = Net::Request::makeByteArray(m_spec.purpur ? purpurProjectUrl() : PaperApi::projectUrl(m_spec.id));
    PaperApi::addUserAgent(request);

    auto job = makeShared<NetJob>(tr("Paper versions"), APPLICATION->network());
    job->addNetAction(request);
    connect(job.get(), &Task::succeeded, this, [this, generation, body] {
        if (generation != m_generation)
            return;
        const auto document = Json::requireDocument(*body, "Server versions");
        if (!document) {
            m_status->setText(document.error());
            return;
        }
        const auto root = document.value().object();
        m_allVersions.clear();
        if (m_spec.purpur) {
            for (const auto& value : root.value("versions").toArray()) {
                const auto version = value.toString();
                if (!version.isEmpty())
                    m_allVersions.append(version);
            }
        } else {
            const auto groups = root.value("versions").toObject();
            for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
                for (const auto& value : it.value().toArray()) {
                    const auto version = value.toString();
                    if (!version.isEmpty())
                        m_allVersions.append(version);
                }
            }
        }
        m_loaded = true;
        showVersions(m_allVersions);
    });
    connect(job.get(), &Task::failed, this, [this, generation](const QString& reason) {
        if (generation != m_generation)
            return;
        m_status->setText(reason);
    });
    m_job = job;
    m_job->start();
}

void PaperPage::onPreReleasesToggled()
{
    if (m_loaded)
        showVersions(m_allVersions);
}

void PaperPage::showVersions(const QStringList& listed)
{
    const auto selected = m_versions->currentItem() ? m_versions->currentItem()->data(Qt::UserRole).toString() : QString();
    auto versions = listed;
    const bool showPreReleases = m_preReleases->isChecked();
    if (!showPreReleases) {
        versions.removeIf([](const QString& version) { return version.contains('-'); });
    }
    std::ranges::sort(versions, [](const QString& left, const QString& right) { return Version(left) > Version(right); });

    m_versions->blockSignals(true);
    m_versions->clear();
    for (const auto& version : versions) {
        auto* item = new QListWidgetItem(version, m_versions);
        item->setData(Qt::UserRole, version);
    }
    m_versions->blockSignals(false);

    if (m_versions->count() == 0) {
        m_status->setText(tr("No %1 versions found.").arg(m_spec.name));
        m_dialog->setSuggestedPack();
        return;
    }

    auto matches = m_versions->findItems(selected, Qt::MatchExactly);
    m_versions->setCurrentItem(matches.isEmpty() ? m_versions->item(0) : matches.first());
}

void PaperPage::onVersionChanged()
{
    const auto* item = m_versions->currentItem();
    if (!item) {
        m_dialog->setSuggestedPack();
        return;
    }
    fetchStableBuild(item->data(Qt::UserRole).toString());
}

void PaperPage::fetchStableBuild(const QString& version)
{
    const int generation = ++m_generation;
    if (m_job)
        m_job->abort();

    m_status->setText(tr("Looking up the latest stable build of %1 %2...").arg(m_spec.name, version));
    m_dialog->setSuggestedPack();

    const auto buildsUrl = m_spec.purpur ? purpurLatestUrl(version) : PaperApi::stableBuildsUrl(m_spec.id, version);
    auto [request, body] = Net::Request::makeByteArray(buildsUrl);
    PaperApi::addUserAgent(request);

    auto job = makeShared<NetJob>(tr("Paper builds"), APPLICATION->network());
    job->addNetAction(request);
    connect(job.get(), &Task::succeeded, this, [this, generation, version, body] {
        if (generation != m_generation)
            return;
        const auto document = Json::requireDocument(*body, "Server builds");
        if (!document) {
            m_status->setText(document.error());
            return;
        }
        if (m_spec.purpur) {
            const auto buildObject = document.value().object();
            const auto build = buildObject.value("build").toString().toInt();
            const auto md5 = buildObject.value("md5").toString();
            if (build <= 0) {
                m_status->setText(tr("Purpur did not provide a server jar for %1.").arg(version));
                return;
            }
            suggestBuild(version, build, purpurDownloadUrl(version, build), md5, QCryptographicHash::Md5);
            return;
        }
        const auto buildsDocument = document.value();
        if (buildsDocument.isObject()) {
            const auto message = buildsDocument.object().value("message").toString();
            m_status->setText(message.isEmpty() ? tr("No stable %1 build for %2.").arg(m_spec.name, version) : message);
            return;
        }
        const auto builds = buildsDocument.array();
        if (builds.isEmpty()) {
            m_status->setText(tr("No stable %1 build for %2.").arg(m_spec.name, version));
            return;
        }

        const auto buildObject = builds.first().toObject();
        const auto download = buildObject.value("downloads").toObject().value("server:default").toObject();
        const auto url = QUrl(download.value("url").toString());
        const auto sha256 = download.value("checksums").toObject().value("sha256").toString();
        const auto build = buildObject.value("id").toInt();
        if (!url.isValid() || build <= 0) {
            m_status->setText(tr("%1 did not provide a server jar for %2.").arg(m_spec.name, version));
            return;
        }
        suggestBuild(version, build, url, sha256, QCryptographicHash::Sha256);
    });
    connect(job.get(), &Task::failed, this, [this, generation](const QString& reason) {
        if (generation != m_generation)
            return;
        m_status->setText(reason);
    });
    m_job = job;
    m_job->start();
}

void PaperPage::suggestBuild(const QString& version,
                             int build,
                             const QUrl& url,
                             const QString& checksum,
                             QCryptographicHash::Algorithm algorithm)
{
    m_status->setText(tr("%1 %2, stable build %3").arg(m_spec.name, version, QString::number(build)));
    m_dialog->setSuggestedPack(tr("%1 %2").arg(m_spec.name, version),
                               new PaperServerCreationTask(m_spec.id, m_spec.name, version, build, url, checksum, algorithm));
}
