# SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.21)
project(plasma-tv VERSION 0.1.0 LANGUAGES CXX)

# Keep debug symbols by default so crash backtraces are useful.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Don't export template instances from the executable. Otherwise libraries that
# carry their own copies (e.g. QtKeychain built against an older Qt) are bound
# to ours at runtime, which crashed in QList<QVariant> code when talking to KWallet.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Network Sql Multimedia DBus)
find_package(Qt6Keychain REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(plasma-tv src/main.cpp)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(plasma-tv
    URI PlasmaTv
    VERSION 1.0
    # Shared Qt build: QML plugins (incl. Kirigami) are loaded at runtime.
    NO_IMPORT_SCAN
    SOURCES
        src/serverconfig.h src/serverconfig.cpp
        src/colorschemes.h src/colorschemes.cpp
        src/networkauth.h src/networkauth.cpp
        src/appsettings.h src/appsettings.cpp
        src/tvhapi.h src/tvhapi.cpp
        src/m3u.h src/m3u.cpp
        src/xmltv.h src/xmltv.cpp
        src/streamproxy.h src/streamproxy.cpp
        src/epgdatabase.h src/epgdatabase.cpp
        src/epgstore.h src/epgstore.cpp
        src/channelmodel.h src/channelmodel.cpp
        src/tvhbackend.h src/tvhbackend.cpp
        src/screensaverinhibitor.h src/screensaverinhibitor.cpp
        src/logoimageprovider.h src/logoimageprovider.cpp
        src/inputdebug.h src/inputdebug.cpp
        src/remotekeys.h src/remotekeys.cpp
        src/streamrecorder.h src/streamrecorder.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/NavBar.qml
        qml/ChannelsPage.qml
        qml/GuidePage.qml
        qml/SettingsPage.qml
        qml/TvButton.qml
        qml/TextEntryDialog.qml
        qml/EventDialog.qml
        qml/PlayerView.qml
        qml/WatchOverlay.qml
        qml/ChannelLogo.qml
        qml/ThinProgress.qml
    RESOURCES
        data/icons/hicolor/scalable/apps/plasmatv.svg
)

option(PLASMA_TV_DEV_TOOLS "Build the scripted UI driver used for screenshots/tests" OFF)
if(PLASMA_TV_DEV_TOOLS)
    target_sources(plasma-tv PRIVATE src/devscript.h src/devscript.cpp)
    target_compile_definitions(plasma-tv PRIVATE PLASMA_TV_DEV_TOOLS)
    # Simulating application focus changes needs QWindowSystemInterface.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_link_libraries(plasma-tv PRIVATE Qt6::GuiPrivate)
endif()

target_include_directories(plasma-tv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(plasma-tv PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Sql Qt6::Multimedia Qt6::DBus
    Qt6Keychain::Qt6Keychain)

include(GNUInstallDirs)
# Kirigami, Bigscreen UI and the Plasma Qt Quick Controls style are only used from QML, so
# they are runtime dependencies (Ubuntu: qml6-module-org-kde-kirigami, qml6-module-org-kde-desktop,
# kde-style-breeze, qt6-svg-plugins; Arch: kirigami, bigscreen-ui, qqc2-desktop-style, breeze,
# qt6-svg).

install(TARGETS plasma-tv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/plasma-tv.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# The icon is called "plasmatv" on purpose: KIconLoader tries the shortened
# "plasma" in the current theme before looking into hicolor, so "plasma-tv"
# would show Breeze's Plasma logo.
install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
