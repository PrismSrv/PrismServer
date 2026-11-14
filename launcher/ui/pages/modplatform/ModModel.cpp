// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModModel.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/ResourceType.h"
#include "ui/pages/modplatform/ResourceModel.h"

#include <QMessageBox>
#include <QModelIndex>
#include <QString>
#include <algorithm>
#include <utility>

namespace ResourceDownload {

ModModel::ModModel(BaseInstance& baseInst,
                   ResourceFolderModel* resourceList,
                   const ResourceAPI* api,
                   const QString& debugName,
                   QString metaEntryBase)
    : ResourceModel(resourceList, api)
    , m_baseInstance(baseInst)
    , m_debugName(debugName + " (Model)")
    , m_metaEntryBase(std::move(metaEntryBase))
{}

/******** Make data requests ********/

ResourceAPI::SearchArgs ModModel::createSearchArguments()
{
    auto* profile = static_cast<const MinecraftInstance&>(m_baseInstance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    std::optional<std::vector<Version>> versions{};
    std::optional<QStringList> categories{};
    auto loaders = profile->getSupportedModLoaders();

    // Version filter
    if (!m_filter->versions.empty()) {
        versions = m_filter->versions;
    }
    if (m_filter->loaders != 0U) {
        loaders = m_filter->loaders;
    }
    if (!m_filter->categoryIds.empty()) {
        categories = m_filter->categoryIds;
    }
    applyPluginConstraints(versions, loaders, categories, true);
    auto side = m_filter->side;

    auto sort = getCurrentSortingMethodByIndex();

    return {
        .type = m_resourceType,
        .offset = m_nextSearchOffset,
        .search = m_searchTerm,
        .sorting = sort,
        .loaders = loaders,
        .versions = versions,
        .side = side,
        .categoryIds = categories,
        .openSource = m_filter->openSource,
        .excludeDisclosureTypes = m_filter->excludeDisclosureTypes,
    };
}

ResourceAPI::VersionSearchArgs ModModel::createVersionsArguments(const QModelIndex& index)
{
    auto pack = m_packs[index.row()];
    auto* profile = static_cast<const MinecraftInstance&>(m_baseInstance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    std::optional<std::vector<Version>> versions{};
    auto loaders = profile->getSupportedModLoaders();
    if (!m_filter->versions.empty()) {
        versions = m_filter->versions;
    }
    if (m_filter->loaders != 0U) {
        loaders = m_filter->loaders;
    }

    std::optional<QStringList> platformLoaders{};
    applyPluginConstraints(versions, loaders, platformLoaders, false);

    return { .pack = pack,
             .mcVersions = versions,
             .loaders = loaders,
             .platformLoaders = platformLoaders,
             .resourceType = m_resourceType };
}

void ModModel::applyPluginConstraints(std::optional<std::vector<Version>>& versions,
                                      std::optional<ModPlatform::ModLoaderTypes>& loaders,
                                      std::optional<QStringList>& categories,
                                      bool forSearch)
{
    if (m_resourceType != ModPlatform::ResourceType::Plugin) {
        return;
    }

    auto& instance = static_cast<MinecraftInstance&>(m_baseInstance);
    const auto software = instance.settings()->get("ServerSoftware").toString();
    const auto serverVersion = instance.settings()->get("ServerVersion").toString();
    // Velocity's own version (for example 4.3.1) is not a Minecraft version. Modrinth lists these plugins by category.
    const bool proxy = software == QLatin1String("velocity");
    if (proxy && versions.has_value() && !serverVersion.isEmpty()) {
        std::erase_if(*versions, [&](const Version& version) { return version.toString() == serverVersion; });
        if (versions->empty()) {
            versions.reset();
        }
    }

    const bool missingVersion = !versions.has_value() || versions->empty() ||
                                (versions->size() == 1 && versions->front().toString().isEmpty());
    if (missingVersion && !proxy && !serverVersion.isEmpty()) {
        versions = std::vector<Version>{ Version(serverVersion) };
    }
    loaders.reset();

    if (software.isEmpty() || m_api != &ModrinthAPI::get()) {
        return;
    }
    if (forSearch && categories.has_value() && !categories->isEmpty()) {
        return;
    }
    categories = QStringList{ software };
}

void ModModel::searchWithTerm(const QString& term, unsigned int sort, bool filterChanged)
{
    if (m_searchTerm == term && m_searchTerm.isNull() == term.isNull() && m_currentSortIndex == sort && !filterChanged) {
        return;
    }

    setSearchTerm(term);
    m_currentSortIndex = sort;

    refresh();
}

namespace {

bool checkSide(ModPlatform::SideType filter, ModPlatform::SideType value)
{
    return (filter != ModPlatform::SideType::ClientSide && filter != ModPlatform::SideType::ServerSide) ||
           (value != ModPlatform::SideType::ClientSide && value != ModPlatform::SideType::ServerSide) || filter == value;
}
}  // namespace

bool ModModel::checkFilters(ModPlatform::IndexedPack::Ptr pack)
{
    if (!m_filter) {
        return true;
    }
    return !(m_filter->hideInstalled && isPackInstalled(pack)) && checkSide(m_filter->side, pack->side);
}

bool ModModel::checkVersionFilters(const ModPlatform::IndexedVersion& v)
{
    if (!m_filter) {
        return true;
    }
    auto loaders = static_cast<MinecraftInstance&>(m_baseInstance).getPackProfile()->getSupportedModLoaders();
    if (m_filter->loaders != 0U) {
        loaders = m_filter->loaders;
    }
    auto matchesGame = m_filter->checkMcVersions(v.mcVersion);
    if (!matchesGame && m_resourceType == ModPlatform::ResourceType::Plugin) {
        auto& instance = static_cast<MinecraftInstance&>(m_baseInstance);
        const auto serverVersion = instance.settings()->get("ServerVersion").toString();
        if (instance.settings()->get("ServerSoftware").toString() == QLatin1String("velocity") && !serverVersion.isEmpty()) {
            ModFilterWidget::Filter withoutProxy = *m_filter;
            std::erase_if(withoutProxy.versions, [&](const Version& version) { return version.toString() == serverVersion; });
            matchesGame = withoutProxy.checkMcVersions(v.mcVersion);
        }
    }
    return (!optedOut(v) &&                                                                   // is opted out(aka curseforge download link)
            (!loaders.has_value() || !v.loaders || ((loaders.value() & v.loaders) != 0U)) &&  // loaders
            checkSide(m_filter->side, v.side) &&                                              // side
            (m_filter->releases.empty() ||                                                    // releases
             std::find(m_filter->releases.cbegin(), m_filter->releases.cend(), v.versionType) != m_filter->releases.cend()) &&
            matchesGame);  // mcVersions
}

}  // namespace ResourceDownload
