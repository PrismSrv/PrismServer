// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#include "ServerPropertiesWidget.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/server/ServerFiles.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

ServerPropertiesWidget::ServerPropertiesWidget(MinecraftInstance* instance, QWidget* parent)
    : QScrollArea(parent), m_instance(instance)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    build();
}

void ServerPropertiesWidget::addField(QFormLayout* form,
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

    editor->setToolTip(key);
    auto* name = new QLabel(label, parent);
    name->setToolTip(key);
    name->setBuddy(editor);
    form->addRow(name, editor);
    m_fields.insert(key, Field{ kind, editor, fallback, min, max });
    m_order.append(key);
}

void ServerPropertiesWidget::build()
{
    auto* inner = new QWidget(this);
    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* note = new QLabel(tr("These settings are saved in server.properties. Restart the server to apply them."), inner);
    note->setWordWrap(true);
    layout->addWidget(note);

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
    addGroup(QStringLiteral("players"), tr("Players"));
    addGroup(QStringLiteral("world"), tr("World"));
    addGroup(QStringLiteral("messages"), tr("Messages"));
    addGroup(QStringLiteral("remote"), tr("Query and RCON"));
    addGroup(QStringLiteral("resource"), tr("Resource pack"));
    addGroup(QStringLiteral("management"), tr("Management server"));
    addGroup(QStringLiteral("advanced"), tr("Advanced"));

    auto add = [&](const QString& group, const QString& key, const QString& label, Kind kind, const char* fallback) {
        addField(forms.value(group), key, label, kind, QString::fromLatin1(fallback), 0, 0, {}, {});
    };
    auto addNumber = [&](const QString& group, const QString& key, const QString& label, const char* fallback, int min, int max) {
        addField(forms.value(group), key, label, Kind::Integer, QString::fromLatin1(fallback), min, max, {}, {});
    };
    auto addChoice = [&](const QString& group, const QString& key, const QString& label, const char* fallback, const QStringList& choices,
                         const QStringList& labels) {
        addField(forms.value(group), key, label, Kind::Choice, QString::fromLatin1(fallback), 0, 0, choices, labels);
    };

    addNumber("network", "server-port", tr("Server port"), "25565", 1, 65535);
    add("network", "server-ip", tr("Server IP"), Kind::Text, "");
    add("network", "online-mode", tr("Online mode"), Kind::Boolean, "true");
    add("network", "prevent-proxy-connections", tr("Prevent proxy connections"), Kind::Boolean, "false");
    addNumber("network", "network-compression-threshold", tr("Compression threshold"), "256", -1, 65535);
    addNumber("network", "rate-limit", tr("Rate limit"), "0", 0, 999999999);
    add("network", "use-native-transport", tr("Native transport"), Kind::Boolean, "true");
    add("network", "enable-status", tr("Enable status"), Kind::Boolean, "true");
    add("network", "hide-online-players", tr("Hide online players"), Kind::Boolean, "false");
    addNumber("network", "status-heartbeat-interval", tr("Status heartbeat interval"), "0", 0, 999999999);
    add("network", "accepts-transfers", tr("Accept transfers"), Kind::Boolean, "false");
    add("network", "log-ips", tr("Log IP addresses"), Kind::Boolean, "true");

    addNumber("players", "max-players", tr("Max players"), "20", 0, 999999999);
    add("players", "white-list", tr("Whitelist"), Kind::Boolean, "false");
    add("players", "enforce-whitelist", tr("Enforce whitelist"), Kind::Boolean, "false");
    addNumber("players", "player-idle-timeout", tr("Idle timeout (minutes)"), "0", 0, 999999999);
    addNumber("players", "op-permission-level", tr("Operator permission level"), "4", 0, 4);
    addNumber("players", "function-permission-level", tr("Function permission level"), "2", 1, 4);
    add("players", "enforce-secure-profile", tr("Enforce secure profile"), Kind::Boolean, "true");

    add("world", "level-name", tr("World name"), Kind::Text, "world");
    add("world", "level-seed", tr("Seed"), Kind::Text, "");
    addChoice("world", "level-type", tr("World type"), "minecraft:normal",
              { "minecraft:normal", "minecraft:flat", "minecraft:large_biomes", "minecraft:amplified", "minecraft:single_biome_surface" },
              { tr("Normal"), tr("Flat"), tr("Large biomes"), tr("Amplified"), tr("Single biome") });
    add("world", "generator-settings", tr("Generator settings"), Kind::Text, "{}");
    addChoice("world", "gamemode", tr("Game mode"), "survival", { "survival", "creative", "adventure", "spectator" },
              { tr("Survival"), tr("Creative"), tr("Adventure"), tr("Spectator") });
    add("world", "force-gamemode", tr("Force game mode"), Kind::Boolean, "false");
    addChoice("world", "difficulty", tr("Difficulty"), "easy", { "peaceful", "easy", "normal", "hard" },
              { tr("Peaceful"), tr("Easy"), tr("Normal"), tr("Hard") });
    add("world", "hardcore", tr("Hardcore"), Kind::Boolean, "false");
    add("world", "allow-flight", tr("Allow flight"), Kind::Boolean, "false");
    add("world", "generate-structures", tr("Generate structures"), Kind::Boolean, "true");
    addNumber("world", "view-distance", tr("View distance"), "10", 2, 128);
    addNumber("world", "simulation-distance", tr("Simulation distance"), "10", 2, 128);
    addNumber("world", "spawn-protection", tr("Spawn protection"), "16", 0, 1024);
    addNumber("world", "max-world-size", tr("Max world size"), "29999984", 1, 29999984);
    addNumber("world", "max-tick-time", tr("Max tick time (ms)"), "60000", -1, 999999999);
    addNumber("world", "entity-broadcast-range-percentage", tr("Entity broadcast range (%)"), "100", 0, 1000);
    addNumber("world", "pause-when-empty-seconds", tr("Pause when empty (seconds)"), "-1", -1, 999999999);
    add("world", "sync-chunk-writes", tr("Sync chunk writes"), Kind::Boolean, "true");
    addChoice("world", "region-file-compression", tr("Region compression"), "deflate", { "deflate", "none" }, { tr("Deflate"), tr("None") });
    addNumber("world", "max-chained-neighbor-updates", tr("Max chained neighbor updates"), "1000000", 0, 999999999);

    add("messages", "motd", tr("Message of the day"), Kind::Text, "A Minecraft Server");
    add("messages", "broadcast-console-to-ops", tr("Broadcast console to operators"), Kind::Boolean, "true");
    add("messages", "broadcast-rcon-to-ops", tr("Broadcast RCON to operators"), Kind::Boolean, "true");
    add("messages", "bug-report-link", tr("Bug report link"), Kind::Text, "");

    add("remote", "enable-query", tr("Enable query"), Kind::Boolean, "false");
    addNumber("remote", "query.port", tr("Query port"), "25565", 1, 65535);
    add("remote", "enable-rcon", tr("Enable RCON"), Kind::Boolean, "false");
    addNumber("remote", "rcon.port", tr("RCON port"), "25575", 1, 65535);
    add("remote", "rcon.password", tr("RCON password"), Kind::Text, "");

    add("resource", "resource-pack", tr("Resource pack URL"), Kind::Text, "");
    add("resource", "resource-pack-sha1", tr("Resource pack SHA-1"), Kind::Text, "");
    add("resource", "resource-pack-id", tr("Resource pack ID"), Kind::Text, "");
    add("resource", "resource-pack-prompt", tr("Resource pack prompt"), Kind::Text, "");
    add("resource", "require-resource-pack", tr("Require resource pack"), Kind::Boolean, "false");

    add("management", "management-server-enabled", tr("Enabled"), Kind::Boolean, "false");
    add("management", "management-server-host", tr("Host"), Kind::Text, "localhost");
    addNumber("management", "management-server-port", tr("Port"), "0", 0, 65535);
    add("management", "management-server-secret", tr("Secret"), Kind::Text, "");
    add("management", "management-server-allowed-origins", tr("Allowed origins"), Kind::Text, "");
    add("management", "management-server-tls-enabled", tr("TLS"), Kind::Boolean, "true");
    add("management", "management-server-tls-keystore", tr("Keystore"), Kind::Text, "");
    add("management", "management-server-tls-keystore-password", tr("Keystore password"), Kind::Text, "");

    add("advanced", "enable-jmx-monitoring", tr("JMX monitoring"), Kind::Boolean, "false");
    add("advanced", "enable-code-of-conduct", tr("Code of conduct"), Kind::Boolean, "false");
    add("advanced", "debug", tr("Debug"), Kind::Boolean, "false");
    addNumber("advanced", "chat-spam-threshold-seconds", tr("Chat spam threshold (seconds)"), "10", 0, 999999999);
    addNumber("advanced", "command-spam-threshold-seconds", tr("Command spam threshold (seconds)"), "10", 0, 999999999);
    add("advanced", "text-filtering-config", tr("Text filtering config"), Kind::Text, "");
    addNumber("advanced", "text-filtering-version", tr("Text filtering version"), "0", 0, 100);
    add("advanced", "initial-enabled-packs", tr("Initial enabled packs"), Kind::Text, "vanilla");
    add("advanced", "initial-disabled-packs", tr("Initial disabled packs"), Kind::Text, "");

    m_extraGroup = new QGroupBox(tr("Other"), inner);
    m_extraForm = new QFormLayout(m_extraGroup);
    m_extraForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_extraForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_extraGroup->setVisible(false);
    layout->addWidget(m_extraGroup);
    layout->addStretch();
    setWidget(inner);
}

void ServerPropertiesWidget::clearExtra()
{
    for (const auto& key : m_extraKeys) {
        m_fields.remove(key);
        m_loaded.remove(key);
        m_order.removeAll(key);
    }
    m_extraKeys.clear();
    if (!m_extraForm)
        return;
    while (auto* item = m_extraForm->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void ServerPropertiesWidget::applyValue(const QString& key, const QString& value)
{
    const auto field = m_fields.value(key);
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

QString ServerPropertiesWidget::valueOf(const QString& key) const
{
    const auto field = m_fields.value(key);
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

void ServerPropertiesWidget::load()
{
    const auto file = ServerFiles::readAll(m_instance->gameRoot());
    clearExtra();
    for (const auto& key : QStringList(m_order)) {
        if (!m_fields.contains(key))
            continue;
        const auto value = file.values.contains(key) ? file.values.value(key) : m_fields.value(key).fallback;
        applyValue(key, value);
        m_loaded.insert(key, valueOf(key));
    }
    for (const auto& key : file.order) {
        if (m_fields.contains(key))
            continue;
        addField(m_extraForm, key, key, Kind::Text, file.values.value(key), 0, 0, {}, {});
        m_extraKeys.append(key);
        m_loaded.insert(key, valueOf(key));
    }
    m_extraGroup->setVisible(!m_extraKeys.isEmpty());
}

bool ServerPropertiesWidget::save()
{
    QList<ServerFiles::PropertyValue> changed;
    for (const auto& key : m_order) {
        if (!m_fields.contains(key))
            continue;
        const auto now = valueOf(key);
        if (now == m_loaded.value(key))
            continue;
        changed.append({ key, now });
    }
    if (changed.isEmpty())
        return true;
    if (!ServerFiles::writeValues(m_instance->gameRoot(), changed)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write server.properties."));
        return false;
    }
    for (const auto& entry : changed)
        m_loaded.insert(entry.key, entry.value);
    return true;
}
