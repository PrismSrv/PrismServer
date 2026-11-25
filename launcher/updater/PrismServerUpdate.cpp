// SPDX-License-Identifier: GPL-3.0-only

#include "PrismServerUpdate.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>

#include <climits>
#include <optional>

#include "Version.h"

namespace {
// Release version of this build. Keep it equal to the GitHub tag, without the leading v.
constexpr auto kCurrentVersion = "0.1.4";
constexpr auto kAssetName = "PrismServer.exe";
constexpr auto kReleasesUrl = "https://api.github.com/repos/PrismSrv/PrismServer/releases?per_page=20";

struct ReleaseOffer {
    QString version;
    QString notes;
    QUrl url;
};

QNetworkRequest requestFor(const QUrl& url, bool api)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PrismServer/0.1 (https://github.com/PrismSrv/PrismServer)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    if (api)
        request.setRawHeader("Accept", "application/vnd.github+json");
    return request;
}

QString versionFromTag(QString tag)
{
    if (tag.startsWith(u'v') || tag.startsWith(u'V'))
        tag.remove(0, 1);
    if (tag.isEmpty() || !tag.front().isDigit())
        return {};
    return tag;
}

std::optional<ReleaseOffer> newestRelease(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isArray())
        return {};

    bool found = false;
    Version best;
    ReleaseOffer offer;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject release = value.toObject();
        if (release.value(QStringLiteral("draft")).toBool())
            continue;
        const QString versionText = versionFromTag(release.value(QStringLiteral("tag_name")).toString());
        if (versionText.isEmpty())
            continue;

        QString downloadUrl;
        for (const QJsonValue& assetValue : release.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = assetValue.toObject();
            if (asset.value(QStringLiteral("name")).toString() == QLatin1String(kAssetName))
                downloadUrl = asset.value(QStringLiteral("browser_download_url")).toString();
        }
        if (downloadUrl.isEmpty())
            continue;

        const Version version(versionText);
        if (found && !(version > best))
            continue;
        found = true;
        best = version;
        offer.version = versionText;
        offer.notes = release.value(QStringLiteral("body")).toString();
        offer.url = QUrl(downloadUrl);
    }

    if (!found || !(best > Version(QString::fromLatin1(kCurrentVersion))))
        return {};
    return offer;
}

QString recordedLauncherPath()
{
    QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("launcher.path")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool hasPackageMagic(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 24)
        return false;
    if (!file.seek(file.size() - 8))
        return false;
    return file.read(8) == QByteArray("PRISMPKG", 8);
}

}  // namespace

PrismServerUpdate::PrismServerUpdate(QNetworkAccessManager* network, QWidget* dialogParent, QObject* parent)
    : QObject(parent), m_network(network), m_dialogParent(dialogParent)
{}

void PrismServerUpdate::check()
{
    if (m_checked || !m_network)
        return;
    m_checked = true;

    QNetworkReply* reply = m_network->get(requestFor(QUrl(QString::fromLatin1(kReleasesUrl)), true));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qInfo() << "Update check failed:" << reply->errorString();
            return;
        }
        const std::optional<ReleaseOffer> offer = newestRelease(reply->readAll());
        if (!offer) {
            qInfo() << "PrismServer" << kCurrentVersion << "is the newest release.";
            return;
        }
        qInfo() << "PrismServer update available:" << offer->version;
        ask(offer->version, offer->notes, offer->url);
    });
}

void PrismServerUpdate::ask(const QString& version, const QString& releaseNotes, const QUrl& downloadUrl)
{
    QMessageBox box(QMessageBox::Information, tr("Update PrismServer"),
                    tr("PrismServer %1 is available. You are running %2.").arg(version, QString::fromLatin1(kCurrentVersion)), QMessageBox::NoButton,
                    m_dialogParent);
    QPushButton* updateButton = box.addButton(tr("Update"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(updateButton);
    const QString notes = releaseNotes.trimmed();
    if (!notes.isEmpty())
        box.setDetailedText(notes);
    box.exec();
    if (box.clickedButton() == updateButton)
        download(version, downloadUrl);
}

void PrismServerUpdate::download(const QString& version, const QUrl& downloadUrl)
{
    QNetworkReply* reply = m_network->get(requestFor(downloadUrl, false));
    auto* progress = new QProgressDialog(tr("Downloading PrismServer %1…").arg(version), tr("Cancel"), 0, 0, m_dialogParent);
    progress->setWindowTitle(tr("Update PrismServer"));
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 received, qint64 total) {
        if (total <= 0)
            return;
        progress->setMaximum(static_cast<int>(qMin(total, static_cast<qint64>(INT_MAX))));
        progress->setValue(static_cast<int>(qMin(received, static_cast<qint64>(INT_MAX))));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, version] {
        progress->deleteLater();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                QMessageBox::warning(m_dialogParent, tr("Update PrismServer"),
                                     tr("The download did not finish.\n%1").arg(reply->errorString()));
            }
            return;
        }

        const QByteArray body = reply->readAll();
        const QString tempPath = QDir(QDir::tempPath()).filePath(QStringLiteral("PrismServer-update.exe"));
        QFile::remove(tempPath);
        QFile file(tempPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(body) != static_cast<qint64>(body.size())) {
            QMessageBox::warning(m_dialogParent, tr("Update PrismServer"), tr("PrismServer could not save the download."));
            return;
        }
        file.close();
        if (!hasPackageMagic(tempPath)) {
            QFile::remove(tempPath);
            QMessageBox::warning(m_dialogParent, tr("Update PrismServer"), tr("The downloaded file is not a PrismServer update."));
            return;
        }
        install(version, tempPath);
    });
}

void PrismServerUpdate::install(const QString& version, const QString& downloadedPath)
{
    QString destination = recordedLauncherPath();
    if (destination.isEmpty())
        destination = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../PrismServer.exe")));
    QDir().mkpath(QFileInfo(destination).absolutePath());

    const QString staging = destination + QStringLiteral(".new");
    QFile::remove(staging);
    if (!QFile::copy(downloadedPath, staging)) {
        QFile::remove(downloadedPath);
        QMessageBox::warning(m_dialogParent, tr("Update PrismServer"), tr("PrismServer could not save the update."));
        return;
    }
    QFile::remove(downloadedPath);

    if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(staging);
        QMessageBox::warning(m_dialogParent, tr("Update PrismServer"),
                             tr("Close %1, then open PrismServer again to finish the update.").arg(QDir::toNativeSeparators(destination)));
        return;
    }
    if (!QFile::rename(staging, destination)) {
        QMessageBox::warning(m_dialogParent, tr("Update PrismServer"), tr("PrismServer could not replace the installed file."));
        return;
    }

    qInfo() << "Opening PrismServer" << version << "from" << destination;
    if (!QProcess::startDetached(destination, {})) {
        QMessageBox::warning(m_dialogParent, tr("Update PrismServer"), tr("The update was saved, but PrismServer could not open it."));
        return;
    }
    QCoreApplication::quit();
}
