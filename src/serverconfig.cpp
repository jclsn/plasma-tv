// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "serverconfig.h"

namespace
{
// Accepts "host:port/path" and local file paths as well as full URLs.
QUrl sourceUrl(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url = QUrl::fromUserInput(trimmed);
    const QString scheme = url.scheme();
    if (!url.isValid()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")
            && scheme != QLatin1String("file"))) {
        return {};
    }

    return url;
}
}

ServerConfig::Backend ServerConfig::backendFromString(const QString &name)
{
    return name == QLatin1String("m3u") ? Backend::M3u : Backend::Tvheadend;
}

QString ServerConfig::backendName(Backend backend)
{
    return backend == Backend::M3u ? QStringLiteral("m3u") : QStringLiteral("tvheadend");
}

bool ServerConfig::isValid() const
{
    if (backend == Backend::M3u) {
        return playlist().isValid();
    }

    return baseUrl().isValid() && !host.trimmed().isEmpty();
}

QUrl ServerConfig::playlist() const
{
    return sourceUrl(playlistUrl);
}

QUrl ServerConfig::xmltv() const
{
    return sourceUrl(xmltvUrl);
}

QUrl ServerConfig::baseUrl() const
{
    if (backend == Backend::M3u) {
        const QUrl source = playlist();
        if (!source.isValid()) {
            return {};
        }

        QUrl url;
        url.setScheme(source.scheme());
        url.setHost(source.host());
        url.setPort(source.port());
        url.setPath(QStringLiteral("/"));
        return url;
    }

    QString hostPart = host.trimmed();
    bool useTls = https;
    int effectivePort = port;

    // Be forgiving: accept "http://host:port/webroot" style input in the host field.
    if (hostPart.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        useTls = true;
        hostPart = hostPart.mid(8);
    } else if (hostPart.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
        hostPart = hostPart.mid(7);
    }

    QString path;
    const int slash = hostPart.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        path = hostPart.mid(slash);
        hostPart = hostPart.left(slash);
    }

    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }

    if (hostPart.startsWith(QLatin1Char('['))) {
        // IPv6 literal, optionally followed by ":port"
        const int close = hostPart.indexOf(QLatin1Char(']'));
        if (close > 0 && hostPart.size() > close + 2
            && hostPart.at(close + 1) == QLatin1Char(':')) {
            bool ok = false;
            const int explicitPort = hostPart.mid(close + 2).toInt(&ok);
            if (ok) {
                effectivePort = explicitPort;
            }

            hostPart = hostPart.left(close + 1);
        }
    } else if (hostPart.count(QLatin1Char(':')) == 1) {
        bool ok = false;
        const int explicitPort = hostPart.section(QLatin1Char(':'), 1).toInt(&ok);
        if (ok) {
            effectivePort = explicitPort;
        }

        hostPart = hostPart.section(QLatin1Char(':'), 0, 0);
    }

    if (hostPart.isEmpty()) {
        return {};
    }

    QUrl url;
    url.setScheme(useTls ? QStringLiteral("https") : QStringLiteral("http"));
    url.setHost(hostPart);

    if (effectivePort > 0) {
        url.setPort(effectivePort);
    }

    url.setPath(path + QLatin1Char('/'));
    return url;
}

QUrl ServerConfig::url(const QString &path) const
{
    QString relative = path;
    while (relative.startsWith(QLatin1Char('/'))) {
        relative.remove(0, 1);
    }

    return baseUrl().resolved(QUrl(relative));
}

QString ServerConfig::identity() const
{
    if (backend == Backend::M3u) {
        return QStringLiteral("m3u:") + playlist().toString(QUrl::RemoveUserInfo) + QLatin1Char(' ')
            + xmltv().toString(QUrl::RemoveUserInfo);
    }

    return baseUrl().toString(QUrl::RemoveUserInfo);
}
