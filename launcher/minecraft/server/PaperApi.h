// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismServer - Minecraft server launcher
 *  Based on Prism Launcher, which is licensed under GPL-3.0-only.
 *  PrismServer is not affiliated with Prism Launcher.
 */

#pragma once

#include <QUrl>
#include <QUrlQuery>

#include "net/RawHeaderProxy.h"
#include "net/Request.h"

namespace PaperApi {

inline QByteArray userAgent()
{
    return QByteArrayLiteral("PrismServer/0.1 (https://prismlauncher.org)");
}

inline void addUserAgent(const Net::Request::Ptr& request)
{
    auto proxy = std::make_unique<Net::RawHeaderProxy>();
    proxy->addHeader(QByteArrayLiteral("User-Agent"), userAgent());
    request->addHeaderProxy(std::move(proxy));
}

inline QUrl projectUrl(const QString& project)
{
    return QUrl(QStringLiteral("https://fill.papermc.io/v3/projects/%1").arg(project));
}

inline QUrl stableBuildsUrl(const QString& project, const QString& version)
{
    const auto encoded = QString::fromUtf8(QUrl::toPercentEncoding(version));
    QUrl url(QStringLiteral("https://fill.papermc.io/v3/projects/%1/versions/%2/builds").arg(project, encoded));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("channel"), QStringLiteral("STABLE"));
    url.setQuery(query);
    return url;
}

}  // namespace PaperApi
