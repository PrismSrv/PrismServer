// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "VelocitySettingsWidget.h"

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/server/VelocityNetwork.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int VersionRole = Qt::UserRole;
constexpr int SoftwareRole = Qt::UserRole + 1;
constexpr int RootRole = Qt::UserRole + 2;
constexpr int PortRole = Qt::UserRole + 3;
constexpr int NameRole = Qt::UserRole + 4;

QString fieldId(const QString& section, const QString& key)
{
    return section + QLatin1Char('\n') + key;
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.mid(1, value.size() - 2);
        value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    return value;
}

QString quote(const QString& value)
{
    QString out = QStringLiteral("\"");
    for (const QChar ch : value) {
        if (ch == '\\' || ch == '"')
            out += '\\';
        out += ch;
    }
    out += '"';
    return out;
}

struct TomlEntry {
    QString section;
    QString key;
    QString rendered;
};

QString tomlPath(MinecraftInstance* instance)
{
    return FS::PathCombine(instance->gameRoot(), QStringLiteral("velocity.toml"));
}

QHash<QString, QString> readToml(const QString& path)
{
    QHash<QString, QString> values;
    const auto data = FS::read(path);
    if (!data)
        return values;

    QString section;
    for (QString line : QString::fromUtf8(*data).split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        const int eq = trimmed.indexOf('=');
        if (eq <= 0)
            continue;
        const auto key = trimmed.left(eq).trimmed();
        values.insert(fieldId(section, key), unquote(trimmed.mid(eq + 1)));
    }
    return values;
}

bool writeToml(const QString& path, const QList<TomlEntry>& changes)
{
    if (changes.isEmpty())
        return true;

    QStringList lines;
    if (const auto data = FS::read(path)) {
        lines = QString::fromUtf8(*data).split('\n');
        for (auto& line : lines) {
            if (line.endsWith('\r'))
                line.chop(1);
        }
        while (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
    }

    QHash<QString, QString> pending;
    for (const auto& change : changes)
        pending.insert(fieldId(change.section, change.key), change.rendered);

    QString section;
    QSet<QString> seenSections;
    QHash<QString, int> insertAt;
    insertAt.insert(QString(), lines.size());
    bool seenHeader = false;
    QSet<QString> written;
    for (int i = 0; i < lines.size(); ++i) {
        const auto trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']') && !trimmed.startsWith('#')) {
            if (!seenHeader) {
                insertAt.insert(QString(), i);
                seenHeader = true;
            }
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            seenSections.insert(section);
            insertAt.insert(section, i + 1);
            continue;
        }
        insertAt.insert(section, i + 1);
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        const int eq = trimmed.indexOf('=');
        if (eq <= 0)
            continue;
        const auto key = trimmed.left(eq).trimmed();
        const auto id = fieldId(section, key);
        if (!pending.contains(id))
            continue;
        lines[i] = key + QStringLiteral(" = ") + pending.value(id);
        written.insert(id);
    }

    struct Insertion {
        int at = 0;
        QString line;
    };
    QList<Insertion> insertions;
    QHash<QString, QStringList> newSections;
    for (const auto& change : changes) {
        const auto id = fieldId(change.section, change.key);
        if (written.contains(id))
            continue;
        const auto line = change.key + QStringLiteral(" = ") + change.rendered;
        if (!change.section.isEmpty() && !seenSections.contains(change.section) && !insertAt.contains(change.section)) {
            newSections[change.section].append(line);
            continue;
        }
        insertions.append({ insertAt.value(change.section, lines.size()), line });
        insertAt[change.section] = insertAt.value(change.section, lines.size()) + 1;
    }
    std::sort(insertions.begin(), insertions.end(), [](const Insertion& a, const Insertion& b) { return a.at > b.at; });
    for (const auto& insertion : insertions)
        lines.insert(insertion.at, insertion.line);
    for (auto it = newSections.cbegin(); it != newSections.cend(); ++it) {
        if (!lines.isEmpty() && !lines.last().isEmpty())
            lines.append(QString());
        lines.append(QLatin1Char('[') + it.key() + QLatin1Char(']'));
        lines.append(it.value());
    }
    return bool(FS::write(path, (lines.join('\n') + '\n').toUtf8()));
}

}  // namespace

VelocitySettingsWidget::VelocitySettingsWidget(MinecraftInstance* instance, QWidget* parent)
    : QScrollArea(parent), m_instance(instance)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    build();
}

void VelocitySettingsWidget::addField(QFormLayout* form,
                                      const QString& section,
                                      const QString& key,
                                      const QString& label,
                                      Kind kind,
                                      const QString& fallback,
                                      int min,
                                      int max,
                                      const QStringList& choiceValues,
                                      const QStringList& choiceLabels)
{
    auto* parent = form->parentWidget();
    QWidget* editor = nullptr;
    switch (kind) {
        case Kind::Boolean: {
            auto* box = new QCheckBox(parent);
            box->setChecked(fallback.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            editor = box;
            break;
        }
        case Kind::Integer: {
            auto* spin = new QSpinBox(parent);
            spin->setRange(min, max);
            spin->setAccelerated(true);
            spin->setValue(fallback.toInt());
            editor = spin;
            break;
        }
        case Kind::Choice: {
            auto* combo = new QComboBox(parent);
            for (int i = 0; i < choiceValues.size() && i < choiceLabels.size(); ++i)
                combo->addItem(choiceLabels.at(i), choiceValues.at(i));
            const int index = combo->findData(fallback);
            if (index >= 0)
                combo->setCurrentIndex(index);
            editor = combo;
            break;
        }
        case Kind::Text: {
            auto* edit = new QLineEdit(parent);
            edit->setText(fallback);
            edit->setClearButtonEnabled(true);
            editor = edit;
            break;
        }
    }

    const auto id = fieldId(section, key);
    editor->setToolTip(section.isEmpty() ? key : section + QStringLiteral(".") + key);
    auto* name = new QLabel(label, parent);
    name->setBuddy(editor);
    form->addRow(name, editor);
    m_fields.insert(id, Field{ kind, section, key, editor, fallback, min, max });
    m_order.append(id);
}

void VelocitySettingsWidget::build()
{
    auto* inner = new QWidget(this);
    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* note = new QLabel(tr("These settings are saved in velocity.toml. Restart the proxy to apply them."), inner);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* routing = new QGroupBox(tr("Player routing"), inner);
    auto* routingLayout = new QVBoxLayout(routing);
    auto* intro = new QLabel(
        tr("Choose the Paper and Purpur servers this proxy connects to. Only servers of the same Minecraft version can be selected together. "
           "The checked server closest to the top is where players join first. Restart the proxy and those backends after changing this."),
        routing);
    intro->setWordWrap(true);
    routingLayout->addWidget(intro);

    m_backends = new QListWidget(routing);
    m_backends->setFixedHeight(180);
    routingLayout->addWidget(m_backends);

    auto* orderRow = new QHBoxLayout;
    auto* up = new QPushButton(tr("Move up"), routing);
    auto* down = new QPushButton(tr("Move down"), routing);
    orderRow->addWidget(up);
    orderRow->addWidget(down);
    orderRow->addStretch(1);
    routingLayout->addLayout(orderRow);

    m_routingStatus = new QLabel(routing);
    m_routingStatus->setWordWrap(true);
    routingLayout->addWidget(m_routingStatus);
    layout->addWidget(routing);

    connect(m_backends, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) { onBackendChanged(item); });
    connect(up, &QPushButton::clicked, this, [this] { moveBackend(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveBackend(1); });

    QHash<QString, QFormLayout*> forms;
    auto addGroup = [&](const QString& id, const QString& title) {
        auto* box = new QGroupBox(title, inner);
        auto* form = new QFormLayout(box);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        layout->addWidget(box);
        forms.insert(id, form);
    };

    addGroup(QStringLiteral("network"), tr("Network"));
    addGroup(QStringLiteral("ping"), tr("Ping passthrough"));
    addGroup(QStringLiteral("limiter"), tr("Packet limiter"));
    addGroup(QStringLiteral("advanced"), tr("Advanced"));
    addGroup(QStringLiteral("query"), tr("Query"));

    auto add = [&](const QString& group, const char* section, const char* key, const QString& label, Kind kind, const char* fallback) {
        addField(forms.value(group), QString::fromLatin1(section), QString::fromLatin1(key), label, kind, QString::fromLatin1(fallback), 0,
                 0, {}, {});
    };
    auto addNumber = [&](const QString& group, const char* section, const char* key, const QString& label, const char* fallback, int min,
                         int max) {
        addField(forms.value(group), QString::fromLatin1(section), QString::fromLatin1(key), label, Kind::Integer,
                 QString::fromLatin1(fallback), min, max, {}, {});
    };

    add("network", "", "bind", tr("Bind address"), Kind::Text, "0.0.0.0:25565");
    add("network", "", "motd", tr("Message of the day"), Kind::Text, "A Velocity Server");
    addNumber("network", "", "show-max-players", tr("Shown max players"), "20", 0, 999999999);
    add("network", "", "online-mode", tr("Online mode"), Kind::Boolean, "true");
    add("network", "", "force-key-authentication", tr("Force key authentication"), Kind::Boolean, "true");
    add("network", "", "prevent-client-proxy-connections", tr("Prevent client proxy connections"), Kind::Boolean, "false");
    addField(forms.value(QStringLiteral("network")), QString(), QStringLiteral("player-info-forwarding-mode"), tr("Forwarding mode"),
             Kind::Choice, QStringLiteral("modern"), 0, 0, { "none", "legacy", "bungeeguard", "modern" },
             { tr("None"), tr("Legacy"), tr("BungeeGuard"), tr("Modern") });
    add("network", "", "forwarding-secret-file", tr("Forwarding secret file"), Kind::Text, "forwarding.secret");
    m_secret = new QLineEdit(inner);
    m_secret->setClearButtonEnabled(true);
    forms.value(QStringLiteral("network"))->addRow(tr("Forwarding secret"), m_secret);
    add("network", "", "announce-forge", tr("Announce Forge"), Kind::Boolean, "false");
    add("network", "", "kick-existing-players", tr("Kick existing players"), Kind::Boolean, "false");
    add("network", "", "sample-players-in-ping", tr("Sample players in ping"), Kind::Boolean, "false");
    add("network", "", "enable-player-address-logging", tr("Log player addresses"), Kind::Boolean, "true");

    add("ping", "ping-passthrough", "version", tr("Version"), Kind::Boolean, "false");
    add("ping", "ping-passthrough", "players", tr("Players"), Kind::Boolean, "false");
    add("ping", "ping-passthrough", "description", tr("Description"), Kind::Boolean, "false");
    add("ping", "ping-passthrough", "favicon", tr("Favicon"), Kind::Boolean, "false");
    add("ping", "ping-passthrough", "modinfo", tr("Mod list"), Kind::Boolean, "false");

    addNumber("limiter", "packet-limiter", "interval", tr("Interval (seconds)"), "7", 1, 3600);
    addNumber("limiter", "packet-limiter", "packets-per-second", tr("Packets per second"), "-1", -1, 999999999);
    addNumber("limiter", "packet-limiter", "bytes-per-second", tr("Bytes per second"), "-1", -1, 999999999);
    addNumber("limiter", "packet-limiter", "decompressed-bytes-per-second", tr("Decompressed bytes per second"), "5242880", -1, 999999999);

    addNumber("advanced", "advanced", "compression-threshold", tr("Compression threshold"), "256", -1, 65535);
    addNumber("advanced", "advanced", "compression-level", tr("Compression level"), "-1", -1, 9);
    addNumber("advanced", "advanced", "login-ratelimit", tr("Login rate limit (ms)"), "3000", 0, 999999999);
    addNumber("advanced", "advanced", "connection-timeout", tr("Connection timeout (ms)"), "5000", 0, 999999999);
    addNumber("advanced", "advanced", "read-timeout", tr("Read timeout (ms)"), "30000", 0, 999999999);
    add("advanced", "advanced", "haproxy-protocol", tr("HAProxy protocol"), Kind::Boolean, "false");
    add("advanced", "advanced", "tcp-fast-open", tr("TCP fast open"), Kind::Boolean, "false");
    add("advanced", "advanced", "bungee-plugin-message-channel", tr("BungeeCord plugin channel"), Kind::Boolean, "true");
    add("advanced", "advanced", "show-ping-requests", tr("Show ping requests"), Kind::Boolean, "false");
    add("advanced", "advanced", "failover-on-unexpected-server-disconnect", tr("Failover on disconnect"), Kind::Boolean, "true");
    add("advanced", "advanced", "announce-proxy-commands", tr("Announce proxy commands"), Kind::Boolean, "true");
    add("advanced", "advanced", "log-command-executions", tr("Log commands"), Kind::Boolean, "false");
    add("advanced", "advanced", "log-player-connections", tr("Log player connections"), Kind::Boolean, "true");
    add("advanced", "advanced", "accepts-transfers", tr("Accept transfers"), Kind::Boolean, "false");
    add("advanced", "advanced", "enable-reuse-port", tr("Reuse port"), Kind::Boolean, "false");
    addNumber("advanced", "advanced", "command-rate-limit", tr("Command rate limit (ms)"), "50", 0, 999999999);
    add("advanced", "advanced", "forward-commands-if-rate-limited", tr("Forward rate-limited commands"), Kind::Boolean, "true");
    addNumber("advanced", "advanced", "kick-after-rate-limited-commands", tr("Kick after rate-limited commands"), "0", 0, 999999999);
    addNumber("advanced", "advanced", "tab-complete-rate-limit", tr("Tab-complete rate limit (ms)"), "10", 0, 999999999);
    addNumber("advanced", "advanced", "kick-after-rate-limited-tab-completes", tr("Kick after rate-limited tab completes"), "0", 0,
              999999999);

    add("query", "query", "enabled", tr("Enabled"), Kind::Boolean, "false");
    addNumber("query", "query", "port", tr("Port"), "25565", 1, 65535);
    add("query", "query", "map", tr("Map name"), Kind::Text, "Velocity");
    add("query", "query", "show-plugins", tr("Show plugins"), Kind::Boolean, "false");

    layout->addStretch();
    setWidget(inner);
}

void VelocitySettingsWidget::applyValue(const QString& id, const QString& value)
{
    const auto field = m_fields.value(id);
    if (!field.widget)
        return;
    switch (field.kind) {
        case Kind::Boolean:
            static_cast<QCheckBox*>(field.widget)->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            break;
        case Kind::Integer: {
            bool ok = false;
            int number = value.toInt(&ok);
            if (!ok)
                number = field.fallback.toInt();
            auto* spin = static_cast<QSpinBox*>(field.widget);
            int low = field.min;
            int high = field.max;
            if (number < low)
                low = number;
            if (number > high)
                high = number;
            spin->setRange(low, high);
            spin->setValue(number);
            break;
        }
        case Kind::Choice: {
            auto* combo = static_cast<QComboBox*>(field.widget);
            int index = combo->findData(value);
            if (index < 0) {
                combo->addItem(value, value);
                index = combo->findData(value);
            }
            combo->setCurrentIndex(index);
            break;
        }
        case Kind::Text:
            static_cast<QLineEdit*>(field.widget)->setText(value);
            break;
    }
}

QString VelocitySettingsWidget::valueOf(const QString& id) const
{
    const auto field = m_fields.value(id);
    if (!field.widget)
        return {};
    switch (field.kind) {
        case Kind::Boolean:
            return static_cast<QCheckBox*>(field.widget)->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
        case Kind::Integer:
            return QString::number(static_cast<QSpinBox*>(field.widget)->value());
        case Kind::Choice: {
            const auto selected = static_cast<QComboBox*>(field.widget)->currentData().toString();
            return selected.isEmpty() ? static_cast<QComboBox*>(field.widget)->currentText() : selected;
        }
        case Kind::Text:
            return static_cast<QLineEdit*>(field.widget)->text();
    }
    return {};
}

void VelocitySettingsWidget::load()
{
    const auto values = readToml(tomlPath(m_instance));
    for (const auto& id : m_order) {
        const auto value = values.contains(id) ? values.value(id) : m_fields.value(id).fallback;
        applyValue(id, value);
        m_loaded.insert(id, valueOf(id));
    }

    const auto secretName = valueOf(fieldId(QString(), QStringLiteral("forwarding-secret-file")));
    const auto secretPath = FS::PathCombine(m_instance->gameRoot(), secretName.isEmpty() ? QStringLiteral("forwarding.secret") : secretName);
    QString secret;
    if (const auto bytes = FS::read(secretPath))
        secret = QString::fromUtf8(*bytes);
    if (secret.endsWith('\n'))
        secret.chop(1);
    if (secret.endsWith('\r'))
        secret.chop(1);
    m_secret->setText(secret);
    m_loadedSecret = secret;
    loadRouting();
}

void VelocitySettingsWidget::loadRouting()
{
    m_routingGuard = true;
    m_backends->clear();

    const auto chosen = VelocityNetwork::configuredBackends(m_instance->gameRoot());
    QSet<QString> chosenRoots;
    QList<VelocityBackend> display = chosen;
    for (const auto& backend : chosen)
        chosenRoots.insert(backend.root);
    for (const auto& backend : VelocityNetwork::paperBackends()) {
        if (!chosenRoots.contains(backend.root))
            display.append(backend);
    }

    for (const auto& backend : display) {
        const auto software = backend.software.compare(QStringLiteral("purpur"), Qt::CaseInsensitive) == 0 ? tr("Purpur") : tr("Paper");
        auto* item = new QListWidgetItem(
            tr("%1 %2 — %3 — port %4").arg(software, backend.version, backend.name, QString::number(backend.port)), m_backends);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosenRoots.contains(backend.root) ? Qt::Checked : Qt::Unchecked);
        item->setData(VersionRole, backend.version);
        item->setData(SoftwareRole, backend.software);
        item->setData(RootRole, backend.root);
        item->setData(PortRole, backend.port);
        item->setData(NameRole, backend.name);
    }

    m_routingGuard = false;
    updateRoutingStatus();
    m_loadedRouting = routingKey();
}

QList<VelocityBackend> VelocitySettingsWidget::selectedBackends() const
{
    QList<VelocityBackend> chosen;
    for (int row = 0; row < m_backends->count(); ++row) {
        const auto* item = m_backends->item(row);
        if (item->checkState() != Qt::Checked)
            continue;
        VelocityBackend backend;
        backend.name = item->data(NameRole).toString();
        backend.version = item->data(VersionRole).toString();
        backend.software = item->data(SoftwareRole).toString();
        backend.root = item->data(RootRole).toString();
        backend.port = item->data(PortRole).toInt();
        chosen.append(backend);
    }
    return chosen;
}

QStringList VelocitySettingsWidget::routingKey() const
{
    QStringList keys;
    for (const auto& backend : selectedBackends())
        keys.append(backend.root);
    return keys;
}

void VelocitySettingsWidget::onBackendChanged(QListWidgetItem* item)
{
    if (m_routingGuard || item->checkState() != Qt::Checked) {
        updateRoutingStatus();
        return;
    }

    m_routingGuard = true;
    const auto version = item->data(VersionRole).toString();
    for (int row = 0; row < m_backends->count(); ++row) {
        auto* other = m_backends->item(row);
        if (other != item && other->checkState() == Qt::Checked && other->data(VersionRole).toString() != version)
            other->setCheckState(Qt::Unchecked);
    }
    m_routingGuard = false;
    updateRoutingStatus();
}

void VelocitySettingsWidget::moveBackend(int delta)
{
    const int row = m_backends->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_backends->count())
        return;
    auto* item = m_backends->takeItem(row);
    m_backends->insertItem(target, item);
    m_backends->setCurrentItem(item);
    updateRoutingStatus();
}

void VelocitySettingsWidget::updateRoutingStatus()
{
    const auto chosen = selectedBackends();
    if (chosen.isEmpty()) {
        m_routingStatus->setText(m_backends->count() == 0 ? tr("There are no Paper or Purpur servers yet.")
                                                          : tr("No servers selected. Players will not be routed to a backend."));
        return;
    }
    m_routingStatus->setText(tr("Players join %1 first. All selected servers use Minecraft %2.").arg(chosen.first().name, chosen.first().version));
}

bool VelocitySettingsWidget::save()
{
    QList<TomlEntry> changed;
    for (const auto& id : m_order) {
        const auto now = valueOf(id);
        if (now == m_loaded.value(id))
            continue;
        const auto field = m_fields.value(id);
        QString rendered = now;
        if (field.kind == Kind::Text || field.kind == Kind::Choice)
            rendered = quote(now);
        changed.append({ field.section, field.key, rendered });
    }

    const auto secret = m_secret->text();
    const bool secretChanged = secret != m_loadedSecret;
    const bool routingChanged = routingKey() != m_loadedRouting;
    if (changed.isEmpty() && !secretChanged && !routingChanged)
        return true;

    if (!changed.isEmpty() && !writeToml(tomlPath(m_instance), changed)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write velocity.toml."));
        return false;
    }

    if (secretChanged) {
        const auto secretName = valueOf(fieldId(QString(), QStringLiteral("forwarding-secret-file")));
        const auto secretPath =
            FS::PathCombine(m_instance->gameRoot(), secretName.isEmpty() ? QStringLiteral("forwarding.secret") : secretName);
        if (!FS::write(secretPath, secret.toUtf8())) {
            QMessageBox::critical(this, tr("Error"), tr("Could not write the forwarding secret."));
            return false;
        }
        m_loadedSecret = secret;
    }

    if (routingChanged) {
        QString error;
        if (!VelocityNetwork::updateRouting(m_instance->gameRoot(), selectedBackends(), &error)) {
            QMessageBox::critical(this, tr("Error"), error);
            return false;
        }
        loadRouting();
    }

    for (const auto& entry : changed)
        m_loaded.insert(fieldId(entry.section, entry.key), valueOf(fieldId(entry.section, entry.key)));
    return true;
}
