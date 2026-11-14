// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QCryptographicHash>
#include <QStringList>
#include <QUrl>
#include <QWidget>

#include "net/NetJob.h"
#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QCheckBox;
class QLabel;
class QListWidget;

struct ServerSoftwareSpec {
    QString id;
    QString name;
    QString blurb;
    bool purpur = false;
};

class PaperPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit PaperPage(NewInstanceDialog* dialog, ServerSoftwareSpec spec, QWidget* parent = nullptr);
    ~PaperPage() override;

    QString displayName() const override { return m_spec.name; }
    QIcon icon() const override;
    QString id() const override { return m_spec.id; }
    QString helpPage() const override { return {}; }
    void openedImpl() override;

   private slots:
    void refreshVersions();
    void onPreReleasesToggled();
    void onVersionChanged();

   private:
    void fetchStableBuild(const QString& version);
    void suggestBuild(const QString& version, int build, const QUrl& url, const QString& checksum, QCryptographicHash::Algorithm algorithm);
    void showVersions(const QStringList& versions);

    NewInstanceDialog* m_dialog = nullptr;
    ServerSoftwareSpec m_spec;
    QListWidget* m_versions = nullptr;
    QLabel* m_status = nullptr;
    QCheckBox* m_preReleases = nullptr;
    NetJob::Ptr m_job;
    QStringList m_allVersions;
    bool m_loaded = false;
    int m_generation = 0;
};
