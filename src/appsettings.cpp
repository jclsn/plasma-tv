// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "appsettings.h"

#include "colorschemes.h"
#include "networkauth.h"

#include <QQmlEngine>
#include <QSettings>

#include <qt6keychain/keychain.h>

namespace
{
const QString s_keychainService = QStringLiteral("plasma-tv");
const QString s_keychainKey = QStringLiteral("server-password");
}

AppSettings *AppSettings::instance()
{
    static AppSettings *s_instance = new AppSettings;
    return s_instance;
}

AppSettings *AppSettings::create(QQmlEngine *, QJSEngine *)
{
    auto *self = instance();
    QQmlEngine::setObjectOwnership(self, QQmlEngine::CppOwnership);
    return self;
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("server"));
    m_server.backend =
        ServerConfig::backendFromString(settings.value(QStringLiteral("backend")).toString());
    m_server.host = settings.value(QStringLiteral("host")).toString();
    m_server.port = settings.value(QStringLiteral("port"), 9981).toInt();
    m_server.https = settings.value(QStringLiteral("https"), false).toBool();
    m_server.username = settings.value(QStringLiteral("username")).toString();
    m_server.playlistUrl = settings.value(QStringLiteral("playlistUrl")).toString();
    m_server.xmltvUrl = settings.value(QStringLiteral("xmltvUrl")).toString();
    settings.endGroup();
    m_streamProfile =
        settings.value(QStringLiteral("playback/profile"), QStringLiteral("pass")).toString();
    m_guideDays = qBound(1, settings.value(QStringLiteral("guide/days"), 3).toInt(), 14);
    m_stopInBackground = settings.value(QStringLiteral("playback/stopInBackground"), true).toBool();
    m_lastChannel = settings.value(QStringLiteral("playback/lastChannel")).toString();
    m_colorScheme =
        settings.value(QStringLiteral("appearance/colorScheme"), ColorSchemes::s_default)
            .toString();
    m_colorSchemes = ColorSchemes::available();

    NetworkAuth::setActiveServer(m_server);
    loadPassword();
}

void AppSettings::setLastChannel(const QString &uuid)
{
    if (uuid == m_lastChannel) {
        return;
    }

    m_lastChannel = uuid;
    QSettings().setValue(QStringLiteral("playback/lastChannel"), uuid);
    Q_EMIT lastChannelChanged();
}

void AppSettings::applyColorScheme()
{
    ColorSchemes::apply(m_colorScheme);
}

void AppSettings::previewColorScheme(const QString &id)
{
    ColorSchemes::apply(id);
}

void AppSettings::loadPassword()
{
    if (m_server.username.isEmpty()) {
        m_credentialsReady = true;
        return;
    }

    auto *job = new QKeychain::ReadPasswordJob(s_keychainService, this);
    job->setKey(s_keychainKey);
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error() == QKeychain::NoError) {
            m_server.password = job->textData();
            setSecretStoreStatus({});
        } else if (job->error() != QKeychain::EntryNotFound) {
            qWarning() << "Reading password from secret store failed:" << job->errorString();
            setSecretStoreStatus(tr("Secret store unavailable: %1").arg(job->errorString()));
        }

        NetworkAuth::setActiveServer(m_server);
        m_credentialsReady = true;
        Q_EMIT changed();
        Q_EMIT credentialsReadyChanged();
    });
    job->start();
}

void AppSettings::storePassword(const QString &password)
{
    QKeychain::Job *job;
    if (password.isEmpty()) {
        job = new QKeychain::DeletePasswordJob(s_keychainService, this);
    } else {
        auto *write = new QKeychain::WritePasswordJob(s_keychainService, this);
        write->setTextData(password);
        job = write;
    }

    job->setKey(s_keychainKey);
    connect(job, &QKeychain::Job::finished, this, [this, job, password]() {
        if (job->error() == QKeychain::NoError
            || (password.isEmpty() && job->error() == QKeychain::EntryNotFound)) {
            setSecretStoreStatus({});
        } else {
            qWarning() << "Storing password in secret store failed:" << job->errorString();
            setSecretStoreStatus(
                tr("Password could not be stored securely and is kept for this session only (%1)")
                    .arg(job->errorString()));
        }
    });
    job->start();
}

void AppSettings::setSecretStoreStatus(const QString &status)
{
    if (status == m_secretStoreStatus) {
        return;
    }

    m_secretStoreStatus = status;
    Q_EMIT secretStoreStatusChanged();
}

void AppSettings::save(const QVariantMap &values)
{
    const ServerConfig previous = m_server;

    m_server.backend = ServerConfig::backendFromString(
        values.value(QStringLiteral("backend"), backend()).toString());
    m_server.host = values.value(QStringLiteral("host"), m_server.host).toString().trimmed();
    m_server.port = qBound(1, values.value(QStringLiteral("port"), m_server.port).toInt(), 65535);
    m_server.https = values.value(QStringLiteral("useHttps"), m_server.https).toBool();
    m_server.username =
        values.value(QStringLiteral("username"), m_server.username).toString().trimmed();
    m_server.playlistUrl =
        values.value(QStringLiteral("playlistUrl"), m_server.playlistUrl).toString().trimmed();
    m_server.xmltvUrl =
        values.value(QStringLiteral("xmltvUrl"), m_server.xmltvUrl).toString().trimmed();

    if (values.contains(QStringLiteral("password"))) {
        m_server.password = values.value(QStringLiteral("password")).toString();
    }

    if (m_server.username.isEmpty()) {
        m_server.password.clear();
    }

    m_streamProfile =
        values.value(QStringLiteral("streamProfile"), m_streamProfile).toString().trimmed();

    if (m_streamProfile.isEmpty()) {
        m_streamProfile = QStringLiteral("pass");
    }

    m_guideDays = qBound(1, values.value(QStringLiteral("guideDays"), m_guideDays).toInt(), 14);
    m_stopInBackground =
        values.value(QStringLiteral("stopInBackground"), m_stopInBackground).toBool();
    m_colorScheme = values.value(QStringLiteral("colorScheme"), m_colorScheme).toString();

    QSettings settings;
    settings.beginGroup(QStringLiteral("server"));
    settings.setValue(QStringLiteral("backend"), backend());
    settings.setValue(QStringLiteral("host"), m_server.host);
    settings.setValue(QStringLiteral("port"), m_server.port);
    settings.setValue(QStringLiteral("https"), m_server.https);
    settings.setValue(QStringLiteral("username"), m_server.username);
    settings.setValue(QStringLiteral("playlistUrl"), m_server.playlistUrl);
    settings.setValue(QStringLiteral("xmltvUrl"), m_server.xmltvUrl);
    settings.endGroup();
    settings.setValue(QStringLiteral("playback/profile"), m_streamProfile);
    settings.setValue(QStringLiteral("guide/days"), m_guideDays);
    settings.setValue(QStringLiteral("playback/stopInBackground"), m_stopInBackground);
    settings.setValue(QStringLiteral("appearance/colorScheme"), m_colorScheme);
    applyColorScheme();

    if (m_server.password != previous.password) {
        storePassword(m_server.password);
    }

    NetworkAuth::setActiveServer(m_server);
    Q_EMIT changed();
    Q_EMIT saved(!(m_server == previous));
}
