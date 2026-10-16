/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef M3U_H
#define M3U_H

#include "epgstore.h"
#include "xmltv.h"

#include <QByteArray>
#include <QUrl>
#include <QVector>

namespace M3u
{
struct Playlist {
    QVector<Channel> channels;
    Xmltv::ChannelMap guideMap;
    // Guide announced in the header (url-tvg / x-tvg-url), if any.
    QUrl guideUrl;
};

bool looksLikePlaylist(const QByteArray &data);

// Parses an extended M3U playlist as produced by Dispatcharr, Threadfin and
// IPTV providers. Relative stream and logo URLs are resolved against 'source'.
Playlist parse(const QByteArray &data, const QUrl &source);
}

#endif // M3U_H
