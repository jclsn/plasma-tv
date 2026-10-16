// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamrecorder.h"

#include "appsettings.h"
#include "networkauth.h"
#include "tvhbackend.h"

#include <QDateTime>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace
{
// Marks the files of pauses, so leftovers can be told apart from other recordings.
const QLatin1String s_prefix("Paused - ");
// How often a connection is sent what was recorded meanwhile.
constexpr int s_serveIntervalMs = 100;
// Data handed to a connection but not sent yet; more waits until the player reads it.
constexpr qint64 s_maxPending = 2 * 1024 * 1024;
}

StreamRecorder::StreamRecorder(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_server(new QTcpServer(this))
{
    NetworkAuth::install(m_networkManager);
    removeLeftovers();

    if (m_server->listen(QHostAddress::LocalHost)) {
        connect(m_server, &QTcpServer::newConnection, this, &StreamRecorder::serve);
    }
}

StreamRecorder::~StreamRecorder()
{
    stop();
}

QUrl StreamRecorder::url() const
{
    if (!m_file.isOpen() || !m_server->isListening()) {
        return {};
    }

    return QUrl(QStringLiteral("http://127.0.0.1:%1/recording.ts").arg(m_server->serverPort()));
}

QString StreamRecorder::directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
        + QStringLiteral("/TV recordings");
}

void StreamRecorder::removeLeftovers()
{
    QDir dir(directory());
    const QStringList files = dir.entryList({s_prefix + QStringLiteral("*.ts")}, QDir::Files);

    for (const QString &file : files) {
        dir.remove(file);
    }
}

void StreamRecorder::start(const QString &channelUuid, const QString &channelName)
{
    stop();

    if (!QDir().mkpath(directory())) {
        Q_EMIT failed(tr("Cannot create %1").arg(directory()));

        return;
    }

    // Keep the channel name readable but safe as a file name.
    QString name = channelName;
    name.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("_"));
    const QString time =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh.mm.ss"));
    m_file.setFileName(directory() + QLatin1Char('/') + s_prefix + name + QStringLiteral(" ") + time
                       + QStringLiteral(".ts"));

    if (!m_file.open(QIODevice::WriteOnly)) {
        Q_EMIT failed(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));

        return;
    }

    const QUrl url = TvhBackend::instance()->streamUrl(channelUuid);

    if (!url.isValid() || url.isEmpty()) {
        m_file.close();
        m_file.remove();
        Q_EMIT failed(tr("There is no stream address for this channel"));

        return;
    }

    // Like the guide, a stream on another host than the server never gets the credentials.
    const ServerConfig server = AppSettings::instance()->server();
    m_reply = m_networkManager->get(QNetworkRequest(url));

    if (url.host() == server.baseUrl().host()) {
        NetworkAuth::setReplyCredentials(m_reply, server.username, server.password);
    }

    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        m_file.write(reply->readAll());
        m_file.flush();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // A live stream only ends when stopped or when the connection breaks.
        if (reply == m_reply && reply->error() != QNetworkReply::NoError) {
            Q_EMIT failed(tr("Recording stopped: %1").arg(reply->errorString()));
        }
    });

    Q_EMIT recordingChanged();
}

void StreamRecorder::serve()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        // Only the current recording is served; the request itself doesn't matter.
        auto *file = new QFile(m_file.fileName(), socket);

        if (!m_file.isOpen() || !file->open(QIODevice::ReadOnly)) {
            socket->write("HTTP/1.0 404 Not Found\r\n\r\n");
            socket->disconnectFromHost();

            continue;
        }

        // No length: the player treats it like a live stream and waits for more data.
        socket->write("HTTP/1.0 200 OK\r\nContent-Type: video/mp2t\r\n\r\n");

        auto *timer = new QTimer(socket);
        connect(timer, &QTimer::timeout, socket, [this, socket, file]() {
            // Stopped (or another recording): end the stream.
            if (!m_file.isOpen() || file->fileName() != m_file.fileName()) {
                socket->disconnectFromHost();

                return;
            }

            if (socket->bytesToWrite() < s_maxPending) {
                socket->write(file->read(s_maxPending));
            }
        });
        timer->start(s_serveIntervalMs);
    }
}

void StreamRecorder::stop()
{
    if (!m_reply && !m_file.isOpen()) {
        return;
    }

    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    m_file.close();
    m_file.remove();
    Q_EMIT recordingChanged();
}
