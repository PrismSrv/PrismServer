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

class MinecraftInstance;
class QFormLayout;
class QGroupBox;
class QWidget;

class ServerPropertiesWidget : public QScrollArea {
   public:
    explicit ServerPropertiesWidget(MinecraftInstance* instance, QWidget* parent = nullptr);

    void load();
    bool save();

   private:
    enum class Kind { Boolean, Integer, Text, Choice };

    struct Field {
        Kind kind = Kind::Text;
        QWidget* widget = nullptr;
        QString fallback;
        int min = 0;
        int max = 0;
    };

    void build();
    void addField(QFormLayout* form,
                  const QString& key,
                  const QString& label,
                  Kind kind,
                  const QString& fallback,
                  int min,
                  int max,
                  const QStringList& choiceValues,
                  const QStringList& choiceLabels);
    void clearExtra();
    void applyValue(const QString& key, const QString& value);
    [[nodiscard]] QString valueOf(const QString& key) const;

    MinecraftInstance* m_instance;
    QHash<QString, Field> m_fields;
    QHash<QString, QString> m_loaded;
    QStringList m_order;
    QStringList m_extraKeys;
    QGroupBox* m_extraGroup = nullptr;
    QFormLayout* m_extraForm = nullptr;
};
