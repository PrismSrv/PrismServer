// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QHash>
#include <QScrollArea>
#include <QString>
#include <QStringList>

#include "minecraft/server/VelocityNetwork.h"

class MinecraftInstance;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QWidget;

class VelocitySettingsWidget : public QScrollArea {
   public:
    explicit VelocitySettingsWidget(MinecraftInstance* instance, QWidget* parent = nullptr);

    void load();
    bool save();

   private:
    enum class Kind { Boolean, Integer, Text, Choice };

    struct Field {
        Kind kind = Kind::Text;
        QString section;
        QString key;
        QWidget* widget = nullptr;
        QString fallback;
        int min = 0;
        int max = 0;
    };

    void build();
    void addField(QFormLayout* form,
                  const QString& section,
                  const QString& key,
                  const QString& label,
                  Kind kind,
                  const QString& fallback,
                  int min,
                  int max,
                  const QStringList& choiceValues,
                  const QStringList& choiceLabels);
    void applyValue(const QString& id, const QString& value);
    [[nodiscard]] QString valueOf(const QString& id) const;
    void loadRouting();
    void onBackendChanged(QListWidgetItem* item);
    void moveBackend(int delta);
    void updateRoutingStatus();
    [[nodiscard]] QList<VelocityBackend> selectedBackends() const;
    [[nodiscard]] QStringList routingKey() const;

    MinecraftInstance* m_instance;
    QHash<QString, Field> m_fields;
    QHash<QString, QString> m_loaded;
    QStringList m_order;
    QLineEdit* m_secret = nullptr;
    QString m_loadedSecret;
    QListWidget* m_backends = nullptr;
    QLabel* m_routingStatus = nullptr;
    bool m_routingGuard = false;
    QStringList m_loadedRouting;
};
