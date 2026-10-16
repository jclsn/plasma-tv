// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tvhbackend.h"

#include "appsettings.h"
#include "channelmodel.h"
#include "m3u.h"
#include "streamproxy.h"
#include "tvhapi.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QQmlEngine>
#include <QThread>

#include <algorithm>
#include <memory>

namespace
{
constexpr int s_epgPageSize = 2000;
constexpr int s_maxEpgEvents = 200000;
constexpr int s_tickMs = 15 * 1000;
constexpr int s_epgRefreshMs = 30 * 60 * 1000;
constexpr int s_retryMs = 60 * 1000;

ServerConfig configFromValues(const QVariantMap &values, const ServerConfig &fallback)
{
    ServerConfig config = fallback;
    config.backend = ServerConfig::backendFromString(
        values.value(QStringLiteral("backend"), ServerConfig::backendName(config.backend))
            .toString());
    config.playlistUrl =
        values.value(QStringLiteral("playlistUrl"), config.playlistUrl).toString().trimmed();
    config.xmltvUrl =
        values.value(QStringLiteral("xmltvUrl"), config.xmltvUrl).toString().trimmed();
    config.host = values.value(QStringLiteral("host"), config.host).toString().trimmed();
    config.port = values.value(QStringLiteral("port"), config.port).toInt();
    config.https = values.value(QStringLiteral("useHttps"), config.https).toBool();
    config.username =
        values.value(QStringLiteral("username"), config.username).toString().trimmed();

    if (values.contains(QStringLiteral("password"))) {
        config.password = values.value(QStringLiteral("password")).toString();
    }

    return config;
}

bool isM3u(const ServerConfig &config)
{
    return config.backend == ServerConfig::Backend::M3u;
}

QJsonArray entriesOf(const QJsonDocument &document)
{
    return document.object().value(QLatin1String("entries")).toArray();
}

QString firstUuid(const QJsonDocument &document)
{
    const QJsonValue uuid = document.object().value(QLatin1String("uuid"));
    if (uuid.isArray()) {
        return uuid.toArray().first().toString();
    }

    return uuid.toString();
}
}

TvhBackend *TvhBackend::instance()
{
    static TvhBackend *s_instance = new TvhBackend;
    return s_instance;
}

TvhBackend *TvhBackend::create(QQmlEngine *, QJSEngine *)
{
    auto *self = instance();
    QQmlEngine::setObjectOwnership(self, QQmlEngine::CppOwnership);
    return self;
}

TvhBackend::TvhBackend(QObject *parent)
    : QObject(parent)
    , m_api(new TvhApi(this))
    , m_channels(new ChannelModel(&m_store, this))
    , m_streamProxy(new StreamProxy(this))
{
    m_database.open();

    m_tickTimer.setInterval(s_tickMs);
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
        m_channels->refreshNow();
        Q_EMIT tick();
    });
    m_tickTimer.start();

    m_epgTimer.setInterval(s_epgRefreshMs);
    connect(&m_epgTimer, &QTimer::timeout, this, &TvhBackend::refresh);

    m_retryTimer.setInterval(s_retryMs);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &TvhBackend::refresh);

    AppSettings *settings = AppSettings::instance();
    connect(settings, &AppSettings::saved, this, &TvhBackend::applySettings);
    connect(settings, &AppSettings::credentialsReadyChanged, this, [this]() {
        applySettings(true);
    });

    m_api->setServer(settings->server());
    m_channels->setBaseUrl(settings->server().baseUrl());
    loadCache();

    if (!settings->configured()) {
        setStatus(Unconfigured, tr("Not configured"));
    } else if (settings->credentialsReady()) {
        refresh();
    } else {
        setStatus(Connecting, tr("Unlocking credentials…"));
    }
}

bool TvhBackend::canRecord() const
{
    return !isM3u(m_api->server());
}

void TvhBackend::applySettings(bool serverChanged)
{
    const ServerConfig server = AppSettings::instance()->server();
    if (serverChanged) {
        m_api->setServer(server);
        m_channels->setBaseUrl(server.baseUrl());
        m_serverName.clear();
        m_serverVersion.clear();
        Q_EMIT serverInfoChanged();

        if (m_database.serverIdentity() != server.identity()) {
            clearCache();
        }
    }

    refresh();
}

void TvhBackend::loadCache()
{
    const ServerConfig server = AppSettings::instance()->server();
    if (!server.isValid() || m_database.serverIdentity() != server.identity()) {
        return;
    }

    m_store.setEvents(m_database.loadEvents(QDateTime::currentSecsSinceEpoch()));
    m_channels->setChannels(m_database.loadChannels());
    const qint64 last = m_database.lastUpdate();
    m_lastUpdate = last > 0 ? QDateTime::fromSecsSinceEpoch(last) : QDateTime();
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::clearCache()
{
    m_database.clear();
    m_store.clear();
    m_channels->setChannels({});
    m_lastUpdate = {};
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::refresh()
{
    AppSettings *settings = AppSettings::instance();
    m_retryTimer.stop();

    if (!settings->configured()) {
        setStatus(Unconfigured, tr("Not configured"));
        return;
    }

    if (!settings->credentialsReady()) {
        return;
    }

    const int generation = ++m_generation;
    m_api->abortAll();
    m_api->setServer(settings->server());
    setBusy(true);

    if (m_status != Online) {
        const QString host = settings->server().baseUrl().host();
        setStatus(Connecting,
                  host.isEmpty() ? tr("Loading the playlist…") : tr("Connecting to %1…").arg(host));
    }

    if (isM3u(settings->server())) {
        fetchPlaylist(generation);
        return;
    }

    m_api->get(QStringLiteral("api/serverinfo"), {}, [this, generation](const ApiResult &result) {
        if (generation != m_generation) {
            return;
        }

        if (!result.ok) {
            setBusy(false);
            setStatus(Offline, result.error);
            m_retryTimer.start();
            return;
        }

        const QJsonObject info = result.json().object();
        m_serverName = info.value(QLatin1String("name")).toString(QStringLiteral("Tvheadend"));
        m_serverVersion = info.value(QLatin1String("sw_version")).toString();
        Q_EMIT serverInfoChanged();
        fetchChannels(generation);
    });
}

void TvhBackend::fetchChannels(int generation)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("start"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100000"));
    m_api->get(QStringLiteral("api/channel/grid"),
               query,
               [this, generation](const ApiResult &result) {
                   if (generation != m_generation) {
                       return;
                   }

                   if (!result.ok) {
                       setBusy(false);
                       setStatus(Offline, tr("Loading channels failed: %1").arg(result.error));
                       m_retryTimer.start();
                       return;
                   }

                   QVector<Channel> channels;
                   for (const QJsonValue &value : entriesOf(result.json())) {
                       const QJsonObject object = value.toObject();
                       if (!object.value(QLatin1String("enabled")).toBool(true)) {
                           continue;
                       }

                       channels.append(Channel::fromJson(object));
                   }

                   std::stable_sort(channels.begin(),
                                    channels.end(),
                                    [](const Channel &a, const Channel &b) {
                                        // Unnumbered channels (0) go to the end, alphabetically.
                                        if ((a.number == 0) != (b.number == 0)) {
                                            return a.number != 0;
                                        }

                                        if (a.number != b.number) {
                                            return a.number < b.number;
                                        }

                                        return a.name.localeAwareCompare(b.name) < 0;
                                    });

                   m_database.setServerIdentity(AppSettings::instance()->server().identity());
                   m_database.saveChannels(channels);
                   m_channels->setChannels(channels);
                   setStatus(Online, tr("Connected"));
                   m_epgTimer.start();

                   m_incoming.clear();
                   fetchEpgPage(generation, 0);
               });
}

void TvhBackend::fetchPlaylist(int generation)
{
    const ServerConfig server = m_api->server();
    m_api->get(server.playlist(), [this, generation, server](const ApiResult &result) {
        if (generation != m_generation) {
            return;
        }

        if (!result.ok) {
            setBusy(false);
            setStatus(Offline, tr("Loading the playlist failed: %1").arg(result.error));
            m_retryTimer.start();
            return;
        }

        M3u::Playlist playlist = M3u::parse(result.body, server.playlist());
        if (playlist.channels.isEmpty()) {
            setBusy(false);
            setStatus(Offline,
                      M3u::looksLikePlaylist(result.body)
                          ? tr("The playlist contains no channels")
                          : tr("The address does not point to an M3U playlist"));
            m_retryTimer.start();
            return;
        }

        m_serverName = tr("M3U playlist");
        Q_EMIT serverInfoChanged();
        m_database.setServerIdentity(server.identity());
        m_database.saveChannels(playlist.channels);
        m_channels->setChannels(playlist.channels);
        setStatus(Online, tr("Connected"));
        m_epgTimer.start();

        m_incoming.clear();
        const QUrl guide = server.xmltv().isValid() ? server.xmltv() : playlist.guideUrl;
        if (!guide.isValid()) {
            finishEpg(generation); // no guide: channels only
            return;
        }

        fetchGuide(generation, guide, playlist.guideMap);
    });
}

void TvhBackend::fetchGuide(int generation, const QUrl &url, const Xmltv::ChannelMap &map)
{
    m_api->get(url, [this, generation, map](const ApiResult &result) {
        if (generation != m_generation) {
            return;
        }

        if (!result.ok) {
            setBusy(false);
            Q_EMIT notification(tr("Loading the TV guide failed: %1").arg(result.error));
            return;
        }

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 end = now + qint64(AppSettings::instance()->guideDays()) * 24 * 3600;
        auto events = std::make_shared<QVector<EpgEvent>>();
        auto error = std::make_shared<QString>();

        // Guides of large playlists are tens of megabytes; keep the UI responsive.
        QThread *worker = QThread::create([data = result.body, map, now, end, events, error]() {
            *events = Xmltv::parse(data, map, now, end, error.get());
        });
        connect(worker, &QThread::finished, this, [this, worker, generation, events, error]() {
            worker->deleteLater();
            if (generation != m_generation) {
                return;
            }

            if (!error->isEmpty()) {
                setBusy(false);
                Q_EMIT notification(tr("Loading the TV guide failed: %1").arg(*error));
                return;
            }

            m_incoming = std::move(*events);
            finishEpg(generation);
        });
        worker->start();
    });
}

void TvhBackend::fetchEpgPage(int generation, int offset)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 end = now + qint64(AppSettings::instance()->guideDays()) * 24 * 3600;
    const QJsonArray filter{
        QJsonObject{{QStringLiteral("field"), QStringLiteral("stop")},
                    {QStringLiteral("type"), QStringLiteral("numeric")},
                    {QStringLiteral("comparison"), QStringLiteral("gt")},
                    {QStringLiteral("value"), now}},
        QJsonObject{{QStringLiteral("field"), QStringLiteral("start")},
                    {QStringLiteral("type"), QStringLiteral("numeric")},
                    {QStringLiteral("comparison"), QStringLiteral("lt")},
                    {QStringLiteral("value"), end}},
    };

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("start"), QString::number(offset));
    form.addQueryItem(QStringLiteral("limit"), QString::number(s_epgPageSize));
    form.addQueryItem(QStringLiteral("sort"), QStringLiteral("start"));
    form.addQueryItem(QStringLiteral("dir"), QStringLiteral("ASC"));
    form.addQueryItem(QStringLiteral("filter"),
                      QString::fromUtf8(QJsonDocument(filter).toJson(QJsonDocument::Compact)));

    m_api->post(
        QStringLiteral("api/epg/events/grid"),
        form,
        [this, generation, offset, now, end](const ApiResult &result) {
            if (generation != m_generation) {
                return;
            }

            if (!result.ok) {
                setBusy(false);
                Q_EMIT notification(tr("Loading the TV guide failed: %1").arg(result.error));
                return;
            }

            const QJsonDocument document = result.json();
            const QJsonArray entries = entriesOf(document);
            for (const QJsonValue &value : entries) {
                EpgEvent event = EpgEvent::fromJson(value.toObject());
                // Filter client side as well in case the server ignored the filter.
                if (event.stop > now && event.start < end) {
                    m_incoming.append(std::move(event));
                }
            }

            const int total = document.object().value(QLatin1String("totalCount")).toInt(-1);
            const int fetched = offset + int(entries.size());
            const bool more = entries.size() == s_epgPageSize && (total < 0 || fetched < total)
                && fetched < s_maxEpgEvents;
            if (more) {
                fetchEpgPage(generation, fetched);
            } else {
                finishEpg(generation);
            }
        });
}

void TvhBackend::finishEpg(int generation)
{
    if (generation != m_generation) {
        return;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_database.saveEvents(m_incoming, now);
    m_store.setEvents(std::move(m_incoming));
    m_incoming = {};
    m_lastUpdate = QDateTime::fromSecsSinceEpoch(now);
    m_channels->refreshNow();
    ++m_epgRevision;
    setBusy(false);
    Q_EMIT epgChanged();
}

void TvhBackend::testConnection(const QVariantMap &values)
{
    const ServerConfig config = configFromValues(values, AppSettings::instance()->server());
    if (isM3u(config)) {
        if (!config.playlist().isValid()) {
            Q_EMIT connectionTestFinished(false, tr("Please enter a playlist address"));
            return;
        }

        if (!config.xmltvUrl.isEmpty() && !config.xmltv().isValid()) {
            Q_EMIT connectionTestFinished(false, tr("The guide address is not valid"));
            return;
        }

        m_api->probe(config, config.playlist(), [this, config](const ApiResult &result) {
            if (!result.ok) {
                Q_EMIT connectionTestFinished(false, result.error);
                return;
            }

            if (!M3u::looksLikePlaylist(result.body)) {
                Q_EMIT connectionTestFinished(
                    false,
                    tr("The server answered, but did not send an M3U playlist"));
                return;
            }

            const M3u::Playlist playlist = M3u::parse(result.body, config.playlist());
            const bool guide = config.xmltv().isValid() || playlist.guideUrl.isValid();
            Q_EMIT connectionTestFinished(
                !playlist.channels.isEmpty(),
                playlist.channels.isEmpty()
                    ? tr("The playlist contains no channels")
                    : (guide ? tr("Playlist found: %1 channels").arg(playlist.channels.size())
                             : tr("Playlist found: %1 channels, but no guide address")
                                   .arg(playlist.channels.size())));
        });
        return;
    }

    if (!config.isValid()) {
        Q_EMIT connectionTestFinished(false, tr("Please enter a server address"));
        return;
    }

    m_api->probe(config, QStringLiteral("api/serverinfo"), [this](const ApiResult &result) {
        if (!result.ok) {
            Q_EMIT connectionTestFinished(false, result.error);
            return;
        }

        const QJsonObject info = result.json().object();
        if (!info.contains(QLatin1String("api_version"))) {
            Q_EMIT connectionTestFinished(
                false,
                tr("The server answered, but does not look like TVHeadend"));
            return;
        }

        Q_EMIT connectionTestFinished(
            true,
            tr("Connected to %1 %2")
                .arg(info.value(QLatin1String("name")).toString(QStringLiteral("Tvheadend")),
                     info.value(QLatin1String("sw_version")).toString()));
    });
}

QVariantList TvhBackend::eventsForChannel(const QString &uuid, double fromMs, double toMs) const
{
    QVariantList result;
    const auto events = m_store.range(uuid, qint64(fromMs / 1000.0), qint64(toMs / 1000.0));
    result.reserve(events.size());

    for (const EpgEvent *event : events) {
        result.append(event->toVariant());
    }

    return result;
}

QVariantMap TvhBackend::eventAt(const QString &uuid, double timeMs) const
{
    const EpgEvent *event = m_store.eventAt(uuid, qint64(timeMs / 1000.0));
    return event ? event->toVariant() : QVariantMap();
}

QVariantMap TvhBackend::nextEvent(const QString &uuid, double afterMs) const
{
    const EpgEvent *event = m_store.nextAfter(uuid, qint64(afterMs / 1000.0));
    return event ? event->toVariant() : QVariantMap();
}

QVariantMap TvhBackend::eventById(double id) const
{
    const EpgEvent *event = m_store.byId(qint64(id));
    return event ? event->toVariant() : QVariantMap();
}

void TvhBackend::requestStream(const QString &uuid)
{
    const ServerConfig server = m_api->server();
    if (isM3u(server)) {
        const int row = m_channels->indexOfUuid(uuid);
        QUrl url = row >= 0 ? QUrl(m_channels->channels().at(row).stream) : QUrl();
        if (!url.isValid() || url.isEmpty()) {
            Q_EMIT streamFailed(uuid, tr("The playlist has no stream address for this channel"));
            return;
        }

        // The player can't answer authentication challenges and would log credentials in the
        // URL, so streams from the playlist's own server go through the local relay.
        if (!server.username.isEmpty() && url.userName().isEmpty()
            && url.host() == server.baseUrl().host()) {
            url = m_streamProxy->relay(url, server);
            if (url.isEmpty()) {
                Q_EMIT streamFailed(uuid, tr("The local stream relay could not be started"));
                return;
            }
        }

        Q_EMIT streamReady(uuid, url);
        return;
    }

    const QString profile = AppSettings::instance()->streamProfile();
    QUrlQuery profileQuery;
    profileQuery.addQueryItem(QStringLiteral("profile"), profile);

    QUrl direct = server.url(QStringLiteral("stream/channel/") + uuid);
    direct.setQuery(profileQuery);

    if (server.username.isEmpty()) {
        Q_EMIT streamReady(uuid, direct);
        return;
    }

    // Prefer a short-lived ticket so the password never ends up in the media URL.
    m_api->get(
        QStringLiteral("playlist/ticket/channel/") + uuid,
        profileQuery,
        [this, uuid, server, direct, profileQuery](const ApiResult &result) {
            if (result.ok) {
                const QStringList lines = QString::fromUtf8(result.body).split(QLatin1Char('\n'));
                for (const QString &line : lines) {
                    const QUrl ticketUrl(line.trimmed());

                    if (!ticketUrl.isValid()
                        || !ticketUrl.scheme().startsWith(QLatin1String("http"))) {
                        continue;
                    }

                    // Rebuild against our own base URL: the server may advertise an address we
                    // can't reach.
                    QUrl url = server.baseUrl();
                    url.setPath(ticketUrl.path());
                    QUrlQuery query(ticketUrl);

                    if (!query.hasQueryItem(QStringLiteral("profile"))) {
                        query.addQueryItem(QStringLiteral("profile"),
                                           profileQuery.queryItemValue(QStringLiteral("profile")));
                    }

                    url.setQuery(query);
                    Q_EMIT streamReady(uuid, url);
                    return;
                }
            } else if (result.httpStatus == 401 || result.httpStatus == 403) {
                Q_EMIT streamFailed(uuid, result.error);
                return;
            }

            // Older servers without ticket support: relay the stream with our credentials.
            const QUrl url = m_streamProxy->relay(direct, server);
            if (url.isEmpty()) {
                Q_EMIT streamFailed(uuid, tr("The local stream relay could not be started"));
                return;
            }

            Q_EMIT streamReady(uuid, url);
        });
}

void TvhBackend::record(double eventId)
{
    const EpgEvent *event = m_store.byId(qint64(eventId));
    if (!event || !canRecord()) {
        return;
    }

    const QString title = event->title;
    const qint64 id = event->id;
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("event_id"), QString::number(id));
    form.addQueryItem(QStringLiteral("config_uuid"), QString());
    m_api->post(
        QStringLiteral("api/dvr/entry/create_by_event"),
        form,
        [this, id, title](const ApiResult &result) {
            if (!result.ok) {
                Q_EMIT notification(tr("Could not schedule recording: %1").arg(result.error));
                return;
            }

            const EpgEvent *current = m_store.byId(id);
            const bool running = current && current->start <= QDateTime::currentSecsSinceEpoch();
            setRecordingState(id,
                              firstUuid(result.json()),
                              running ? QStringLiteral("recording") : QStringLiteral("scheduled"));
            Q_EMIT notification(tr("Recording scheduled: %1").arg(title));
        });
}

void TvhBackend::cancelRecording(double eventId)
{
    const EpgEvent *event = m_store.byId(qint64(eventId));
    if (!event || event->dvrUuid.isEmpty() || !canRecord()) {
        return;
    }

    const qint64 id = event->id;
    const QString title = event->title;
    const bool running = event->dvrState == QLatin1String("recording");
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("uuid"), event->dvrUuid);
    const QString path =
        running ? QStringLiteral("api/dvr/entry/stop") : QStringLiteral("api/dvr/entry/cancel");
    m_api->post(path, form, [this, id, title, running](const ApiResult &result) {
        if (!result.ok) {
            Q_EMIT notification(tr("Could not cancel recording: %1").arg(result.error));
            return;
        }

        setRecordingState(id, {}, {});
        Q_EMIT notification(running ? tr("Recording stopped: %1").arg(title)
                                    : tr("Recording cancelled: %1").arg(title));
    });
}

void TvhBackend::setRecordingState(qint64 eventId, const QString &dvrUuid, const QString &dvrState)
{
    EpgEvent *event = m_store.byId(eventId);
    if (!event) {
        return;
    }

    event->dvrUuid = dvrUuid;
    event->dvrState = dvrState;
    m_database.updateRecording(eventId, dvrUuid, dvrState);
    m_channels->refreshNow();
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::setStatus(Status status, const QString &text)
{
    if (status == m_status && text == m_statusText) {
        return;
    }

    m_status = status;
    m_statusText = text;
    Q_EMIT statusChanged();
}

void TvhBackend::setBusy(bool busy)
{
    if (busy == m_busy) {
        return;
    }

    m_busy = busy;
    Q_EMIT busyChanged();
}
