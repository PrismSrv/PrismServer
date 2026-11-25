// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QWidget;

class PrismServerUpdate : public QObject {
    Q_OBJECT
   public:
    PrismServerUpdate(QNetworkAccessManager* network, QWidget* dialogParent, QObject* parent = nullptr);

    void check();

   private:
    void ask(const QString& version, const QString& releaseNotes, const QUrl& downloadUrl);
    void download(const QString& version, const QUrl& downloadUrl);
    void install(const QString& version, const QString& downloadedPath);

    QNetworkAccessManager* m_network;
    QPointer<QWidget> m_dialogParent;
    bool m_checked = false;
};
