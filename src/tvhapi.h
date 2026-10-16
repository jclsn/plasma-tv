/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TVHAPI_H
#define TVHAPI_H

#include "serverconfig.h"

#include <QJsonDocument>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrlQuery>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

struct ApiResult {
    bool ok = false;
    int httpStatus = 0;
    QByteArray body;
    QString error;

    QJsonDocument json() const
    {
        return QJsonDocument::fromJson(body);
    }
};

// Thin asynchronous wrapper around the TVHeadend HTTP API; also fetches M3U
// playlists and XMLTV guides with the same credentials handling.
class TvhApi : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const ApiResult &)>;

    explicit TvhApi(QObject *parent = nullptr);

    void setServer(const ServerConfig &config);
    const ServerConfig &server() const
    {
        return m_server;
    }

    void get(const QString &path, const QUrlQuery &query, Callback callback);
    void get(const QUrl &url, Callback callback);
    void post(const QString &path, const QUrlQuery &form, Callback callback);

    // One-shot request against an arbitrary configuration (used by "Test connection").
    void probe(const ServerConfig &config, const QString &path, Callback callback);
    void probe(const ServerConfig &config, const QUrl &url, Callback callback);

    // Aborts all pending requests without invoking their callbacks.
    void abortAll();

private:
    void track(QNetworkReply *reply, Callback callback);

    QNetworkAccessManager *m_networkManager;
    ServerConfig m_server;
    QSet<QNetworkReply *> m_pending;
};

#endif // TVHAPI_H
