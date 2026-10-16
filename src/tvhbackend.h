/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TVHBACKEND_H
#define TVHBACKEND_H

#include "epgdatabase.h"
#include "epgstore.h"
#include "xmltv.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <qqmlregistration.h>

class ChannelModel;
class StreamProxy;
class TvhApi;
class QQmlEngine;
class QJSEngine;

// Central coordinator: talks to the server (TVHeadend, or an M3U playlist with
// an XMLTV guide), maintains the EPG cache and exposes channels/events to QML.
class TvhBackend : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Tvh)
    QML_SINGLETON

    Q_PROPERTY(ChannelModel *channels READ channels CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString serverName READ serverName NOTIFY serverInfoChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverInfoChanged)
    // Recordings need TVHeadend; M3U sources only play live.
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY serverInfoChanged)
    Q_PROPERTY(QDateTime lastUpdate READ lastUpdate NOTIFY epgChanged)
    Q_PROPERTY(int epgRevision READ epgRevision NOTIFY epgChanged)
    Q_PROPERTY(double epgEnd READ epgEnd NOTIFY epgChanged)
    Q_PROPERTY(double now READ now NOTIFY tick)

public:
    enum Status {
        Unconfigured,
        Connecting,
        Online,
        Offline,
    };
    Q_ENUM(Status)

    static TvhBackend *instance();
    static TvhBackend *create(QQmlEngine *, QJSEngine *);

    ChannelModel *channels() const
    {
        return m_channels;
    }
    Status status() const
    {
        return m_status;
    }
    QString statusText() const
    {
        return m_statusText;
    }
    bool busy() const
    {
        return m_busy;
    }
    QString serverName() const
    {
        return m_serverName;
    }
    QString serverVersion() const
    {
        return m_serverVersion;
    }
    bool canRecord() const;
    QDateTime lastUpdate() const
    {
        return m_lastUpdate;
    }
    int epgRevision() const
    {
        return m_epgRevision;
    }
    double epgEnd() const
    {
        return double(m_store.lastStop()) * 1000.0;
    }
    double now() const
    {
        return double(QDateTime::currentMSecsSinceEpoch());
    }

    Q_INVOKABLE void refresh();
    // values: same keys as Settings.save(); password may be omitted to use the stored one.
    Q_INVOKABLE void testConnection(const QVariantMap &values);
    Q_INVOKABLE void clearCache();

    Q_INVOKABLE QVariantList eventsForChannel(const QString &uuid,
                                              double fromMs,
                                              double toMs) const;
    Q_INVOKABLE QVariantMap eventAt(const QString &uuid, double timeMs) const;
    Q_INVOKABLE QVariantMap nextEvent(const QString &uuid, double afterMs) const;
    Q_INVOKABLE QVariantMap eventById(double id) const;

    Q_INVOKABLE void requestStream(const QString &uuid);
    // The server's address of a channel's stream, without credentials. Empty if unknown.
    QUrl streamUrl(const QString &uuid) const;
    Q_INVOKABLE void record(double eventId);
    Q_INVOKABLE void cancelRecording(double eventId);

Q_SIGNALS:
    void statusChanged();
    void busyChanged();
    void serverInfoChanged();
    void epgChanged();
    void tick();
    void streamReady(const QString &uuid, const QUrl &url);
    void streamFailed(const QString &uuid, const QString &message);
    void connectionTestFinished(bool ok, const QString &message);
    void notification(const QString &message);

private:
    explicit TvhBackend(QObject *parent = nullptr);

    void applySettings(bool serverChanged);
    void loadCache();
    void fetchChannels(int generation);
    void fetchPlaylist(int generation);
    void fetchGuide(int generation, const QUrl &url, const Xmltv::ChannelMap &map);
    void fetchEpgPage(int generation, int offset);
    void finishEpg(int generation);
    void setStatus(Status status, const QString &text);
    void setBusy(bool busy);
    void setRecordingState(qint64 eventId, const QString &dvrUuid, const QString &dvrState);

    TvhApi *m_api;
    ChannelModel *m_channels;
    StreamProxy *m_streamProxy;
    EpgStore m_store;
    EpgDatabase m_database;
    QVector<EpgEvent> m_incoming;

    Status m_status = Unconfigured;
    QString m_statusText;
    bool m_busy = false;
    int m_generation = 0;
    QString m_serverName;
    QString m_serverVersion;
    QDateTime m_lastUpdate;
    int m_epgRevision = 0;

    QTimer m_tickTimer;
    QTimer m_epgTimer;
    QTimer m_retryTimer;
};

#endif // TVHBACKEND_H
