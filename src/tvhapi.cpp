// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tvhapi.h"

#include "networkauth.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace
{
constexpr int s_timeoutMs = 20000;

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(s_timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("plasma-tv/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString describeError(QNetworkReply *reply, int status)
{
    if (status == 401 || reply->error() == QNetworkReply::AuthenticationRequiredError) {
        return QObject::tr("Authentication failed – check username and password");
    }

    if (status == 403) {
        return QObject::tr("Access denied – the user lacks permission on the server");
    }

    if (status == 404) {
        return QObject::tr("Not found (%1)").arg(reply->url().path());
    }

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        return QObject::tr("The server did not respond in time");
    }

    if (reply->error() == QNetworkReply::HostNotFoundError) {
        return QObject::tr("Server address not found");
    }

    if (reply->error() == QNetworkReply::ConnectionRefusedError) {
        return QObject::tr("Connection refused – is the server running on that port?");
    }

    return reply->errorString();
}

// A guide or playlist may live on another host than the server; it never gets the credentials.
void setCredentials(QNetworkReply *reply, const ServerConfig &config)
{
    if (reply->url().host() == config.baseUrl().host()) {
        NetworkAuth::setReplyCredentials(reply, config.username, config.password);
    } else {
        NetworkAuth::setReplyCredentials(reply, {}, {});
    }
}
}

TvhApi::TvhApi(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    NetworkAuth::install(m_networkManager);
}

void TvhApi::setServer(const ServerConfig &config)
{
    if (config == m_server) {
        return;
    }

    abortAll();
    m_server = config;
    m_networkManager->clearAccessCache();
}

void TvhApi::get(const QString &path, const QUrlQuery &query, Callback callback)
{
    QUrl url = m_server.url(path);
    url.setQuery(query);
    get(url, std::move(callback));
}

void TvhApi::get(const QUrl &url, Callback callback)
{
    QNetworkReply *reply = m_networkManager->get(makeRequest(url));
    setCredentials(reply, m_server);
    track(reply, std::move(callback));
}

void TvhApi::post(const QString &path, const QUrlQuery &form, Callback callback)
{
    QNetworkRequest request = makeRequest(m_server.url(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    // QUrlQuery leaves '+' alone, which form decoding would turn into a space.
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8().replace('+', "%2B");
    QNetworkReply *reply = m_networkManager->post(request, body);
    NetworkAuth::setReplyCredentials(reply, m_server.username, m_server.password);
    track(reply, std::move(callback));
}

void TvhApi::probe(const ServerConfig &config, const QString &path, Callback callback)
{
    probe(config, config.url(path), std::move(callback));
}

void TvhApi::probe(const ServerConfig &config, const QUrl &url, Callback callback)
{
    // A fresh manager so cached credentials/connections of the active server don't interfere.
    auto *networkManager = new QNetworkAccessManager(this);
    NetworkAuth::install(networkManager);
    QNetworkReply *reply = networkManager->get(makeRequest(url));
    setCredentials(reply, config);
    connect(reply,
            &QNetworkReply::finished,
            this,
            [reply, networkManager, callback = std::move(callback)]() {
                ApiResult result;
                result.httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result.body = reply->readAll();
                result.ok = reply->error() == QNetworkReply::NoError;

                if (!result.ok) {
                    result.error = describeError(reply, result.httpStatus);
                }

                reply->deleteLater();
                networkManager->deleteLater();
                callback(result);
            });
}

void TvhApi::abortAll()
{
    const auto pending = m_pending;
    m_pending.clear();

    for (QNetworkReply *reply : pending) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void TvhApi::track(QNetworkReply *reply, Callback callback)
{
    m_pending.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() {
        m_pending.remove(reply);
        ApiResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.body = reply->readAll();
        result.ok = reply->error() == QNetworkReply::NoError;

        if (!result.ok) {
            result.error = describeError(reply, result.httpStatus);
        }

        reply->deleteLater();
        callback(result);
    });
}
