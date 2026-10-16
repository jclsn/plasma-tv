// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "m3u.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>

namespace
{
struct Entry {
    QHash<QString, QString> attributes;
    QString title;
};

// Splits '#EXTINF:-1 tvg-id="a" group-title="x, y",Title' into attributes and title.
Entry parseExtinf(QStringView line)
{
    static const QRegularExpression s_attribute(
        QStringLiteral(R"re(([A-Za-z0-9_-]+)="([^"]*)")re"));

    Entry entry;
    qsizetype comma = -1;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line.at(i) == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (line.at(i) == QLatin1Char(',') && !quoted) {
            comma = i;
            break;
        }
    }

    const QStringView head = comma >= 0 ? line.left(comma) : line;
    entry.title = comma >= 0 ? line.mid(comma + 1).trimmed().toString() : QString();

    auto it = s_attribute.globalMatchView(head);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        entry.attributes.insert(match.captured(1).toLower(), match.captured(2).trimmed());
    }

    return entry;
}

QString attribute(const Entry &entry, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        const QString value = entry.attributes.value(QLatin1String(name));
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

QUrl resolve(const QUrl &source, const QString &reference)
{
    return reference.isEmpty() ? QUrl() : source.resolved(QUrl(reference));
}

QString key(const QString &text)
{
    return text.simplified().toLower();
}
}

bool M3u::looksLikePlaylist(const QByteArray &data)
{
    QByteArray start = data.left(1024).trimmed();
    if (start.startsWith("\xEF\xBB\xBF")) {
        start = start.mid(3).trimmed();
    }

    return start.startsWith("#EXTM3U") || start.contains("#EXTINF");
}

M3u::Playlist M3u::parse(const QByteArray &data, const QUrl &source)
{
    Playlist playlist;
    QSet<QString> usedUuids;
    std::optional<Entry> pending;

    const QString text = QString::fromUtf8(data);
    const auto lines = QStringView(text).split(QLatin1Char('\n'));

    for (QStringView line : lines) {
        line = line.trimmed();
        if (line.startsWith(QChar(0xFEFF))) {
            line = line.mid(1);
        }

        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith(QLatin1String("#EXTM3U"))) {
            const Entry header = parseExtinf(line);
            // Several guides may be listed comma separated; the first one is used.
            const QString guide = attribute(header, {"url-tvg", "x-tvg-url", "tvg-url"})
                                      .section(QLatin1Char(','), 0, 0)
                                      .trimmed();
            playlist.guideUrl = resolve(source, guide);
            continue;
        }

        if (line.startsWith(QLatin1String("#EXTINF:"))) {
            pending = parseExtinf(line.mid(8));
            continue;
        }

        if (line.startsWith(QLatin1Char('#'))) {
            continue; // #EXTGRP, #EXTVLCOPT, comments …
        }

        if (!pending) {
            continue; // plain M3U without metadata isn't useful as a channel list
        }

        const Entry entry = std::move(*pending);
        pending.reset();

        Channel channel;
        channel.stream = resolve(source, line.toString()).toString();
        channel.name = entry.title;
        const QString tvgName = attribute(entry, {"tvg-name"});
        if (channel.name.isEmpty()) {
            channel.name = tvgName;
        }

        if (channel.name.isEmpty()) {
            channel.name = QObject::tr("Channel %1").arg(playlist.channels.size() + 1);
        }

        channel.icon = resolve(source, attribute(entry, {"tvg-logo", "logo"})).toString();
        // Decimal numbers ("2.1") keep their major part.
        channel.number =
            int(attribute(entry, {"tvg-chno", "channel-number", "tvg-num"}).toDouble());

        // Stable ids keep the last watched channel across playlist updates.
        const QString tvgId = attribute(entry, {"tvg-id", "channel-id"});
        const QString base = !tvgId.isEmpty() ? tvgId : channel.name;
        channel.uuid = base;
        for (int n = 2; usedUuids.contains(channel.uuid); ++n) {
            channel.uuid = base + QLatin1Char('#') + QString::number(n);
        }

        usedUuids.insert(channel.uuid);

        if (!tvgId.isEmpty()) {
            playlist.guideMap.byId[key(tvgId)].append(channel.uuid);
        }

        playlist.guideMap.byName[key(channel.name)].append(channel.uuid);
        if (!tvgName.isEmpty() && key(tvgName) != key(channel.name)) {
            playlist.guideMap.byName[key(tvgName)].append(channel.uuid);
        }

        playlist.channels.append(std::move(channel));
    }

    const bool numbered = std::any_of(playlist.channels.cbegin(),
                                      playlist.channels.cend(),
                                      [](const Channel &channel) {
                                          return channel.number > 0;
                                      });
    if (!numbered) {
        // Number by position so channels can still be picked with the number keys.
        for (int i = 0; i < playlist.channels.size(); ++i) {
            playlist.channels[i].number = i + 1;
        }
    } else {
        // Unnumbered channels go to the end, otherwise the playlist order is kept.
        std::stable_sort(playlist.channels.begin(),
                         playlist.channels.end(),
                         [](const Channel &a, const Channel &b) {
                             if ((a.number == 0) != (b.number == 0)) {
                                 return a.number != 0;
                             }

                             return a.number < b.number;
                         });
    }

    return playlist;
}
