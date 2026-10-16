// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "xmltv.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <algorithm>

namespace
{
QString key(const QString &text)
{
    return text.simplified().toLower();
}

int digits(QStringView text, qsizetype from, qsizetype count, bool *ok)
{
    int value = 0;
    for (qsizetype i = from; i < from + count; ++i) {
        if (i >= text.size() || !text.at(i).isDigit()) {
            *ok = false;
            return 0;
        }

        value = value * 10 + text.at(i).digitValue();
    }

    return value;
}

struct Programme {
    QString channel;
    qint64 start = 0;
    qint64 stop = 0;
    QString title;
    QString subtitle;
    QString description;
    int genre = 0;
};

// Several translations may be present; the first one is used.
void setOnce(QString &target, const QString &value)
{
    if (target.isEmpty()) {
        target = value.trimmed();
    }
}

Programme readProgramme(QXmlStreamReader &xml)
{
    Programme programme;
    const QXmlStreamAttributes attributes = xml.attributes();
    programme.channel = attributes.value(QLatin1String("channel")).toString();
    programme.start = Xmltv::parseTime(attributes.value(QLatin1String("start")));
    programme.stop = Xmltv::parseTime(attributes.value(QLatin1String("stop")));

    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();
        if (name == QLatin1String("title")) {
            setOnce(programme.title, xml.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (name == QLatin1String("sub-title")) {
            setOnce(programme.subtitle,
                    xml.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (name == QLatin1String("desc")) {
            setOnce(programme.description,
                    xml.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (name == QLatin1String("category")) {
            const int genre = Xmltv::genreFromCategory(
                xml.readElementText(QXmlStreamReader::IncludeChildElements));
            if (programme.genre == 0) {
                programme.genre = genre;
            }
        } else {
            xml.skipCurrentElement();
        }
    }

    return programme;
}

QStringList readChannelNames(QXmlStreamReader &xml)
{
    QStringList names;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("display-name")) {
            names.append(xml.readElementText(QXmlStreamReader::IncludeChildElements));
        } else {
            xml.skipCurrentElement();
        }
    }

    return names;
}
}

qint64 Xmltv::parseTime(QStringView text)
{
    text = text.trimmed();
    bool ok = true;
    const int year = digits(text, 0, 4, &ok);
    const int month = digits(text, 4, 2, &ok);
    const int day = digits(text, 6, 2, &ok);
    if (!ok) {
        return 0;
    }

    // Hours, minutes and seconds may be left out.
    qsizetype position = 8;
    int time[3] = {0, 0, 0};
    for (int &part : time) {
        bool partOk = true;
        const int value = digits(text, position, 2, &partOk);
        if (!partOk) {
            break;
        }

        part = value;
        position += 2;
    }

    const QDateTime dateTime(QDate(year, month, day),
                             QTime(time[0], time[1], time[2]),
                             QTimeZone::UTC);
    if (!dateTime.isValid()) {
        return 0;
    }

    // Without a zone the time is UTC.
    qint64 offset = 0;
    const QStringView zone = text.mid(position).trimmed();
    if (zone.size() >= 5 && (zone.at(0) == QLatin1Char('+') || zone.at(0) == QLatin1Char('-'))) {
        bool zoneOk = true;
        const int hours = digits(zone, 1, 2, &zoneOk);
        const int minutes = digits(zone, 3, 2, &zoneOk);
        if (zoneOk) {
            offset = (hours * 3600 + minutes * 60) * (zone.at(0) == QLatin1Char('-') ? -1 : 1);
        }
    }

    return dateTime.toSecsSinceEpoch() - offset;
}

int Xmltv::genreFromCategory(const QString &category)
{
    const QString text = category.toLower();
    const auto any = [&text](std::initializer_list<const char *> words) {
        return std::any_of(words.begin(), words.end(), [&text](const char *word) {
            return text.contains(QLatin1String(word));
        });
    };

    // Order matters: "Kinderfilm" is for kids, "Dokumentarfilm" a documentary.
    if (any({"kid", "child", "kinder", "animation", "cartoon", "zeichentrick"})) {
        return 0x50;
    }
    if (any({"sport", "football", "fußball", "soccer"})) {
        return 0x40;
    }
    if (any({"news", "nachrichten", "weather", "wetter"})) {
        return 0x20;
    }
    if (any({"docu",
             "doku",
             "education",
             "bildung",
             "science",
             "wissen",
             "nature",
             "natur",
             "history",
             "geschichte"})) {
        return 0x90;
    }
    if (any({"music", "musik", "concert", "konzert"})) {
        return 0x60;
    }
    if (any({"movie",
             "film",
             "drama",
             "series",
             "serie",
             "crime",
             "krimi",
             "thriller",
             "comedy",
             "komödie",
             "western",
             "sci-fi",
             "horror",
             "romance"})) {
        return 0x10;
    }
    if (any({"show", "entertainment", "unterhaltung", "game", "quiz", "reality", "talk"})) {
        return 0x30;
    }
    if (any({"arts", "culture", "kultur", "kunst"})) {
        return 0x70;
    }
    if (any({"politic", "politik", "society", "gesellschaft", "magazin", "religion"})) {
        return 0x80;
    }
    if (any({"leisure",
             "freizeit",
             "hobby",
             "travel",
             "reise",
             "cooking",
             "kochen",
             "lifestyle",
             "shopping"})) {
        return 0xA0;
    }

    return 0;
}

QVector<EpgEvent>
Xmltv::parse(const QByteArray &data, const ChannelMap &map, qint64 from, qint64 to, QString *error)
{
    QVector<EpgEvent> events;

    if (data.startsWith("\x1F\x8B")) {
        *error = QCoreApplication::translate(
            "Xmltv",
            "The guide is gzip compressed, please use an uncompressed XMLTV address");
        return events;
    }

    // XMLTV channel id → playlist channel uuids
    QHash<QString, QStringList> resolved;
    const auto lookup = [&map, &resolved](const QString &id) -> const QStringList & {
        auto it = resolved.find(id);
        if (it == resolved.end()) {
            // Guides without a <channel> element for this id: match the id itself.
            QStringList uuids = map.byId.value(key(id));
            if (uuids.isEmpty()) {
                uuids = map.byName.value(key(id));
            }

            it = resolved.insert(id, uuids);
        }

        return it.value();
    };

    QVector<Programme> programmes;
    QXmlStreamReader xml(data);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("tv")) {
        *error = QCoreApplication::translate("Xmltv", "The guide is not an XMLTV document");
        return events;
    }

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("channel")) {
            const QString id = xml.attributes().value(QLatin1String("id")).toString();
            const QStringList names = readChannelNames(xml);
            QStringList uuids = map.byId.value(key(id));
            for (const QString &name : names) {
                if (!uuids.isEmpty()) {
                    break;
                }

                uuids = map.byName.value(key(name));
            }

            if (uuids.isEmpty()) {
                uuids = map.byName.value(key(id));
            }

            resolved.insert(id, uuids);
        } else if (xml.name() == QLatin1String("programme")) {
            Programme programme = readProgramme(xml);
            if (programme.start > 0 && programme.start < to
                && !lookup(programme.channel).isEmpty()) {
                programmes.append(std::move(programme));
            }
        } else {
            xml.skipCurrentElement();
        }
    }

    if (xml.hasError() && programmes.isEmpty()) {
        *error = QCoreApplication::translate("Xmltv", "The guide could not be read: %1")
                     .arg(xml.errorString());
        return events;
    }

    // The stop time is optional; the next programme on the channel ends it then.
    std::stable_sort(programmes.begin(),
                     programmes.end(),
                     [](const Programme &a, const Programme &b) {
                         return a.channel != b.channel ? a.channel < b.channel : a.start < b.start;
                     });

    for (int i = 0; i < programmes.size(); ++i) {
        Programme &programme = programmes[i];
        if (programme.stop <= programme.start && i + 1 < programmes.size()
            && programmes.at(i + 1).channel == programme.channel) {
            programme.stop = programmes.at(i + 1).start;
        }
    }

    qint64 id = 0;
    for (const Programme &programme : std::as_const(programmes)) {
        if (programme.stop <= programme.start || programme.stop <= from) {
            continue;
        }

        for (const QString &uuid : lookup(programme.channel)) {
            EpgEvent event;
            event.id = ++id;
            event.channel = uuid;
            event.start = programme.start;
            event.stop = programme.stop;
            event.title = programme.title;
            event.subtitle = programme.subtitle;
            event.description = programme.description;
            event.genre = programme.genre;
            events.append(std::move(event));
        }
    }

    return events;
}
