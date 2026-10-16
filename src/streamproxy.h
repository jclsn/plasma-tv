/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STREAMPROXY_H
#define STREAMPROXY_H

#include "serverconfig.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

// Relays streams that need a login through 127.0.0.1, so the media player
// gets an address without credentials. FFmpeg would otherwise need them in
// the URL and prints that URL, password included, to the log.
class StreamProxy : public QObject
{
    Q_OBJECT

public:
    explicit StreamProxy(QObject *parent = nullptr);

    // Returns a local address for 'upstream' that is fetched with the credentials of 'server'.
    // An empty URL means the relay could not be started.
    QUrl relay(const QUrl &upstream, const ServerConfig &server);

private:
    struct Target {
        QUrl url;
        QString username;
        QString password;
    };

    bool listen();
    void handleConnection(QTcpSocket *socket);
    void serve(QTcpSocket *socket, const QByteArray &request);

    QTcpServer *m_server;
    QNetworkAccessManager *m_networkManager;
    QHash<QString, Target> m_targets;
    QStringList m_order; // oldest token first
};

#endif // STREAMPROXY_H
