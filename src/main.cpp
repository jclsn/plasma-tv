// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "appsettings.h"
#include "inputdebug.h"
#include "logoimageprovider.h"
#include "remotekeys.h"
#ifdef PLASMA_TV_DEV_TOOLS
#include "devscript.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    // Bigscreen UI is sized for TVs at 1.5 times its native size, so scale the
    // whole window (popups included) unless the user chose a factor.
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        qputenv("QT_SCALE_FACTOR", "1.5");
    }

    // QApplication (not QGuiApplication): the Plasma Qt Quick Controls style
    // (org.kde.desktop) paints through the widget style.
    QApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("plasma-tv"));
    QGuiApplication::setApplicationName(QStringLiteral("plasma-tv"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Plasma TV"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("plasma-tv"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Fullscreen TVHeadend and M3U/XMLTV client for the living room"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption windowed({QStringLiteral("w"), QStringLiteral("windowed")},
                                QStringLiteral("Start in a window instead of fullscreen"));
    parser.addOption(windowed);
    parser.process(app);

    // Follow Plasma's look; outside a Plasma session fall back to Breeze explicitly.
    const bool plasmaSession =
        qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"));
    if (!plasmaSession
        && QStyleFactory::keys().contains(QStringLiteral("Breeze"), Qt::CaseInsensitive)) {
        QApplication::setStyle(QStringLiteral("Breeze"));
    }

    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        QIcon::setThemeName(QStringLiteral("breeze"));
    }

    // The bundled icon also works when the application isn't installed.
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/qt/qml/PlasmaTv/data/icons/hicolor/scalable/apps/plasmatv.svg")));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    }

    AppSettings::instance()->applyColorScheme();

    RemoteKeys::install(&app);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("logo"), new LogoImageProvider);
    engine.setInitialProperties({{QStringLiteral("startWindowed"), parser.isSet(windowed)}});
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("PlasmaTv", "Main");
    InputDebug::install(&engine);
#ifdef PLASMA_TV_DEV_TOOLS
    DevScript::runFromEnvironment(&engine);
#endif

    return app.exec();
}
