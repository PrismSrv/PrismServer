// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "BaseInstance.h"

#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"

#include "ui/pages/modplatform/ResourceModel.h"
#include "ui/widgets/ModFilterWidget.h"

class Version;

namespace ResourceDownload {

class ModPage;

class ModModel : public ResourceModel {
    Q_OBJECT

   public:
    ModModel(BaseInstance&, ResourceFolderModel*, const ResourceAPI* api, const QString& debugName, QString metaEntryBase);

    /* Ask the API for more information */
    void searchWithTerm(const QString& term, unsigned int sort, bool filterChanged);

    void setFilter(std::shared_ptr<ModFilterWidget::Filter> filter) { m_filter = std::move(filter); }
    void setResourceType(ModPlatform::ResourceType type) { m_resourceType = type; }
    [[nodiscard]] QString debugName() const override { return m_debugName; }
    [[nodiscard]] QString metaEntryBase() const override { return m_metaEntryBase; }

   public slots:
    ResourceAPI::SearchArgs createSearchArguments() override;
    ResourceAPI::VersionSearchArgs createVersionsArguments(const QModelIndex& index) override;

   protected:
    bool checkFilters(ModPlatform::IndexedPack::Ptr pack) override;
    bool checkVersionFilters(const ModPlatform::IndexedVersion& version) override;
    void applyPluginConstraints(std::optional<std::vector<Version>>& versions,
                                std::optional<ModPlatform::ModLoaderTypes>& loaders,
                                std::optional<QStringList>& categories,
                                bool forSearch);

   protected:
    BaseInstance& m_baseInstance;

    std::shared_ptr<ModFilterWidget::Filter> m_filter = nullptr;

   private:
    QString m_debugName;
    QString m_metaEntryBase;
    ModPlatform::ResourceType m_resourceType = ModPlatform::ResourceType::Mod;
};

}  // namespace ResourceDownload
