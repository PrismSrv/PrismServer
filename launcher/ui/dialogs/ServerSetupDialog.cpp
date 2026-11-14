// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "ServerSetupDialog.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/server/ServerFiles.h"
#include "minecraft/server/VelocityNetwork.h"
#include "settings/SettingsObject.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

ServerSetupDialog::ServerSetupDialog(MinecraftInstance* instance, QWidget* parent) : QDialog(parent), m_instance(instance)
{
    setWindowTitle(tr("Start server"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("The first start needs the Minecraft EULA, the server settings, and the memory limit. Later starts use these saved values."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* eulaLink = new QLabel(tr("<a href=\"https://aka.ms/MinecraftEULA\">Minecraft EULA</a>"), this);
    eulaLink->setOpenExternalLinks(true);
    layout->addWidget(eulaLink);

    m_eula = new QCheckBox(tr("I agree to the Minecraft EULA"), this);
    layout->addWidget(m_eula);

    const auto properties = ServerFiles::readProperties(instance->gameRoot());
    auto* settings = instance->settings();

    auto* form = new QFormLayout;
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(properties.port);

    m_onlineMode = new QCheckBox(tr("Online mode"), this);
    m_onlineMode->setChecked(properties.onlineMode);

    m_maxPlayers = new QSpinBox(this);
    m_maxPlayers->setRange(1, 100000);
    m_maxPlayers->setValue(properties.maxPlayers);

    m_minMemory = new QSpinBox(this);
    m_minMemory->setRange(256, 131072);
    m_minMemory->setSingleStep(256);
    m_minMemory->setSuffix(tr(" MB"));
    m_minMemory->setValue(std::max(256, settings->get("MinMemAlloc").toInt()));

    m_maxMemory = new QSpinBox(this);
    m_maxMemory->setRange(256, 131072);
    m_maxMemory->setSingleStep(256);
    m_maxMemory->setSuffix(tr(" MB"));
    m_maxMemory->setValue(std::max(m_minMemory->value(), settings->get("MaxMemAlloc").toInt()));

    form->addRow(tr("Port"), m_port);
    form->addRow(QString(), m_onlineMode);
    form->addRow(tr("Max players"), m_maxPlayers);
    form->addRow(tr("Minimum memory"), m_minMemory);
    form->addRow(tr("Maximum memory"), m_maxMemory);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setEnabled(false);
    layout->addWidget(buttons);

    connect(m_eula, &QCheckBox::toggled, this, [this](bool checked) { m_ok->setEnabled(checked); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ServerSetupDialog::accept()
{
    if (!m_eula->isChecked())
        return;

    int minMemory = m_minMemory->value();
    int maxMemory = m_maxMemory->value();
    if (minMemory > maxMemory)
        std::swap(minMemory, maxMemory);

    ServerFiles::Properties properties;
    properties.port = m_port->value();
    properties.onlineMode = m_onlineMode->isChecked();
    properties.maxPlayers = m_maxPlayers->value();
    const auto root = m_instance->gameRoot();
    if (!ServerFiles::writeEula(root, true) || !ServerFiles::writeProperties(root, properties)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write the server files."));
        return;
    }
    if (m_instance->settings()->get(QStringLiteral("ServerSoftware")).toString() == QLatin1String("velocity") &&
        !VelocityNetwork::syncProxySettings(root, properties.port, properties.onlineMode, properties.maxPlayers)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not update velocity.toml."));
        return;
    }

    {
        const SettingsObject::Lock lock(m_instance->settings());
        m_instance->settings()->set("OverrideMemory", true);
        m_instance->settings()->set("MinMemAlloc", minMemory);
        m_instance->settings()->set("MaxMemAlloc", maxMemory);
    }

    QDialog::accept();
}
