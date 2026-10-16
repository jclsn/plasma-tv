// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamproxy.h"

#include "networkauth.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

namespace
{
constexpr int s_maxTargets = 8;
constexpr qsizetype s_maxRequestSize = 16 * 1024;
// Upstream data is only read while the player keeps up, so memory stays bounded.
constexpr qint64 s_bufferSize = 2 * 1024 * 1024;
constexpr int s_timeoutMs = 30000;

void respond(QTcpSocket *socket, const QByteArray &status)
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    socket->write("HTTP/1.1 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    socket->disconnectFromHost();
}
}

StreamProxy::StreamProxy(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_networkManager(new QNetworkAccessManager(this))
{
    NetworkAuth::install(m_networkManager);
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this, [this]() {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            handleConnection(socket);
        }
    });
}

bool StreamProxy::listen()
{
    if (m_server->isListening()) {
        return true;
    }

    // Loopback only: the relay must never be reachable from the network.
    if (!m_server->listen(QHostAddress::LocalHost)) {
        qWarning() << "Cannot start the stream relay:" << m_server->errorString();
        return false;
    }

    return true;
}

QUrl StreamProxy::relay(const QUrl &upstream, const ServerConfig &server)
{
    if (!listen()) {
        return {};
    }

    // The random token keeps other local users from fetching streams with our credentials.
    quint32 random[4];
    QRandomGenerator::system()->fillRange(random);
    const QString token = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(random), sizeof(random)).toHex());

    m_targets.insert(token, {upstream, server.username, server.password});
    m_order.append(token);
    while (m_order.size() > s_maxTargets) {
        m_targets.remove(m_order.takeFirst());
    }

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(m_server->serverPort());
    url.setPath(QLatin1Char('/') + token);
    return url;
}

void StreamProxy::handleConnection(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

    auto request = std::make_shared<QByteArray>();
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection =
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, request, connection]() {
            request->append(socket->readAll());
            const qsizetype end = request->indexOf("\r\n\r\n");
            if (end < 0) {
                if (request->size() > s_maxRequestSize) {
                    disconnect(*connection);
                    respond(socket, "431 Request Header Fields Too Large");
                }

                return;
            }

            // Anything the player sends after the request is ignored.
            disconnect(*connection);
            serve(socket, request->left(end));
        });
}

void StreamProxy::serve(QTcpSocket *socket, const QByteArray &request)
{
    const QList<QByteArray> requestLine = request.left(request.indexOf("\r\n")).split(' ');
    if (requestLine.size() < 2 || requestLine.at(0) != "GET") {
        respond(socket, "405 Method Not Allowed");
        return;
    }

    const auto target = m_targets.constFind(QString::fromLatin1(requestLine.at(1).mid(1)));
    if (target == m_targets.cend()) {
        respond(socket, "404 Not Found");
        return;
    }

    QNetworkRequest upstreamRequest(target->url);
    upstreamRequest.setTransferTimeout(s_timeoutMs);
    upstreamRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("plasma-tv/0.1"));
    upstreamRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_networkManager->get(upstreamRequest);
    NetworkAuth::setReplyCredentials(reply, target->username, target->password);
    reply->setReadBufferSize(s_bufferSize);

    auto headerSent = std::make_shared<bool>(false);
    const auto forward = [socket, reply, headerSent](bool all) {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }

        if (!all && socket->bytesToWrite() > s_bufferSize) {
            return; // continued from bytesWritten
        }

        if (!*headerSent) {
            *headerSent = true;
            QByteArray type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
            if (type.isEmpty()) {
                type = "application/octet-stream";
            }

            socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type
                          + "\r\nConnection: close\r\n\r\n");
        }

        socket->write(reply->readAll());
    };

    connect(reply, &QNetworkReply::readyRead, socket, [forward]() {
        forward(false);
    });
    connect(socket, &QTcpSocket::bytesWritten, reply, [forward]() {
        forward(false);
    });
    connect(reply, &QNetworkReply::finished, socket, [socket, reply, headerSent, forward]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError && !*headerSent) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            respond(socket,
                    status >= 400 ? QByteArray::number(status) + " Upstream Error"
                                  : QByteArray("502 Bad Gateway"));
            return;
        }

        forward(true);
        socket->disconnectFromHost();
    });
    // The player stopped (channel change): stop downloading as well.
    connect(socket, &QTcpSocket::disconnected, reply, [reply]() {
        reply->abort();
    });
}
