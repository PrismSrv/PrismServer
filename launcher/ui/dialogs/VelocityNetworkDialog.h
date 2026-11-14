// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QDialog>
#include <QList>

#include "minecraft/server/VelocityNetwork.h"

class QLabel;
class QListWidget;
class QListWidgetItem;

class VelocityNetworkDialog : public QDialog {
   public:
    explicit VelocityNetworkDialog(QList<VelocityBackend> backends, QWidget* parent = nullptr);

    QList<VelocityBackend> selected() const;

   private:
    void onItemChanged(QListWidgetItem* item);
    void moveCurrent(int delta);
    void updateStatus();

    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
    bool m_guard = false;
};
