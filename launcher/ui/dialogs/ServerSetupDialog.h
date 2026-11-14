// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QDialog>

class MinecraftInstance;

class ServerSetupDialog : public QDialog {
   public:
    explicit ServerSetupDialog(MinecraftInstance* instance, QWidget* parent = nullptr);

   private:
    void accept() override;

    MinecraftInstance* m_instance = nullptr;
    class QCheckBox* m_eula = nullptr;
    class QSpinBox* m_port = nullptr;
    class QCheckBox* m_onlineMode = nullptr;
    class QSpinBox* m_maxPlayers = nullptr;
    class QSpinBox* m_minMemory = nullptr;
    class QSpinBox* m_maxMemory = nullptr;
    class QPushButton* m_ok = nullptr;
};
