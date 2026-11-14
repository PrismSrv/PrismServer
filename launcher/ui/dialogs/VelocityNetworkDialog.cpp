// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "VelocityNetworkDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int VersionRole = Qt::UserRole;
constexpr int SoftwareRole = Qt::UserRole + 1;
constexpr int RootRole = Qt::UserRole + 2;
constexpr int PortRole = Qt::UserRole + 3;
}  // namespace

VelocityNetworkDialog::VelocityNetworkDialog(QList<VelocityBackend> backends, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Velocity network setup"));
    setModal(true);
    resize(520, 420);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Choose the Paper servers this proxy should connect. Only servers of the same Minecraft version can be selected together. "
           "The checked server closest to the top is where players join first. PrismServer writes the forwarding secret for you."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_list = new QListWidget(this);
    for (const auto& backend : backends) {
        const auto software = backend.software.compare(QStringLiteral("purpur"), Qt::CaseInsensitive) == 0 ? tr("Purpur") : tr("Paper");
        auto* item = new QListWidgetItem(tr("%1 %2 — %3 — port %4").arg(software, backend.version, backend.name, QString::number(backend.port)),
                                         m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(VersionRole, backend.version);
        item->setData(SoftwareRole, backend.software);
        item->setData(RootRole, backend.root);
        item->setData(PortRole, backend.port);
        item->setData(Qt::DisplayRole, item->text());
        item->setData(Qt::UserRole + 4, backend.name);
    }
    layout->addWidget(m_list, 1);

    auto* orderRow = new QHBoxLayout;
    auto* up = new QPushButton(tr("Move up"), this);
    auto* down = new QPushButton(tr("Move down"), this);
    orderRow->addWidget(up);
    orderRow->addWidget(down);
    orderRow->addStretch(1);
    layout->addLayout(orderRow);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Set up"));
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::itemChanged, this, &VelocityNetworkDialog::onItemChanged);
    connect(up, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveCurrent(1); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    updateStatus();
}

QList<VelocityBackend> VelocityNetworkDialog::selected() const
{
    QList<VelocityBackend> chosen;
    for (int row = 0; row < m_list->count(); ++row) {
        const auto* item = m_list->item(row);
        if (item->checkState() != Qt::Checked)
            continue;
        VelocityBackend backend;
        backend.name = item->data(Qt::UserRole + 4).toString();
        backend.version = item->data(VersionRole).toString();
        backend.software = item->data(SoftwareRole).toString();
        backend.root = item->data(RootRole).toString();
        backend.port = item->data(PortRole).toInt();
        chosen.append(backend);
    }
    return chosen;
}

void VelocityNetworkDialog::onItemChanged(QListWidgetItem* item)
{
    if (m_guard || item->checkState() != Qt::Checked)
        updateStatus();
    if (m_guard || item->checkState() != Qt::Checked)
        return;

    m_guard = true;
    const auto version = item->data(VersionRole).toString();
    for (int row = 0; row < m_list->count(); ++row) {
        auto* other = m_list->item(row);
        if (other != item && other->checkState() == Qt::Checked && other->data(VersionRole).toString() != version)
            other->setCheckState(Qt::Unchecked);
    }
    m_guard = false;
    updateStatus();
}

void VelocityNetworkDialog::moveCurrent(int delta)
{
    const int row = m_list->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_list->count())
        return;
    auto* item = m_list->takeItem(row);
    m_list->insertItem(target, item);
    m_list->setCurrentItem(item);
    updateStatus();
}

void VelocityNetworkDialog::updateStatus()
{
    const auto chosen = selected();
    if (chosen.isEmpty()) {
        m_status->setText(m_list->count() == 0 ? tr("There are no Paper servers yet. The proxy will be created without backends.")
                                               : tr("No servers selected. The proxy will be created without backends."));
        return;
    }
    m_status->setText(tr("Players join %1 first. All selected servers use Minecraft %2.").arg(chosen.first().name, chosen.first().version));
}
