// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import PlasmaTv

FocusScope {
    id: root
    signal exitUp
    // Main provides the on-screen keyboard: editText(title, value, mode, callback)
    signal editText(string title, string value, string mode, var callback)

    // Draft values, committed with "Save"
    property string dBackend: "tvheadend"
    readonly property bool m3u: dBackend === "m3u"
    property string dHost
    property int dPort: 9981
    property bool dHttps
    property string dPlaylist
    property string dXmltv
    property string dUser
    property var dPassword: null   // null = unchanged
    property string dProfile
    property int dDays: 3
    property bool dStopInBackground: true
    property string dScheme
    readonly property int schemeIndex: Math.max(0, Settings.colorSchemes.findIndex(s => s.id === dScheme))
    property string testMessage
    property bool testOk: false
    property bool testing: false
    readonly property bool hasSource: m3u ? dPlaylist.length > 0 : dHost.length > 0

    readonly property bool dirty: dBackend !== Settings.backend || dPlaylist !== Settings.playlistUrl || dXmltv !== Settings.xmltvUrl || dHost !== Settings.host || dPort !== Settings.port || dHttps !== Settings.useHttps || dUser !== Settings.username || dPassword !== null || dProfile !== Settings.streamProfile || dDays !== Settings.guideDays || dStopInBackground !== Settings.stopInBackground || dScheme !== Settings.colorScheme

    function load() {
        dBackend = Settings.backend;
        dPlaylist = Settings.playlistUrl;
        dXmltv = Settings.xmltvUrl;
        dHost = Settings.host;
        dPort = Settings.port;
        dHttps = Settings.useHttps;
        dUser = Settings.username;
        dPassword = null;
        dProfile = Settings.streamProfile;
        dDays = Settings.guideDays;
        dStopInBackground = Settings.stopInBackground;
        dScheme = Settings.colorScheme;
    }

    function cycleScheme(delta) {
        const n = Settings.colorSchemes.length;
        dScheme = Settings.colorSchemes[(schemeIndex + delta + n) % n].id;
        // Show the scheme right away; it is only stored by "Save".
        Settings.previewColorScheme(dScheme);
    }

    function values() {
        const v = {
            backend: dBackend,
            playlistUrl: dPlaylist,
            xmltvUrl: dXmltv,
            host: dHost,
            port: dPort,
            useHttps: dHttps,
            username: dUser,
            streamProfile: dProfile,
            guideDays: dDays,
            stopInBackground: dStopInBackground,
            colorScheme: dScheme
        };
        if (dPassword !== null)
            v.password = dPassword;
        return v;
    }

    function save() {
        Settings.save(values());
        dPassword = null;
        testMessage = "";
    }

    Component.onCompleted: load()
    onVisibleChanged: {
        if (visible && !dirty) {
            load();
        } else if (!visible && dScheme !== Settings.colorScheme) {
            // Leaving without saving ends the preview.
            dScheme = Settings.colorScheme;
            Settings.previewColorScheme(dScheme);
        }
    }

    Connections {
        target: Tvh
        function onConnectionTestFinished(ok, message) {
            root.testing = false;
            root.testOk = ok;
            root.testMessage = message;
        }
    }

    // --- Form ---------------------------------------------------------
    Rectangle {
        id: formCard
        x: Theme.margin
        y: 16
        width: 1080
        height: form.implicitHeight + 48
        radius: Theme.radius + 8
        color: Theme.card
        border.color: Theme.outline

        Column {
            id: form
            anchors.fill: parent
            anchors.margins: 24
            spacing: 4

            Text {
                text: qsTr("Server")
                color: Theme.accent
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 2
                leftPadding: 32
                bottomPadding: 8
            }

            SettingRow {
                id: sourceRow
                width: parent.width
                focus: true
                kind: "stepper"
                label: qsTr("Source")
                value: root.m3u ? qsTr("M3U + XMLTV") : qsTr("TVHeadend")
                onDecrement: root.dBackend = root.m3u ? "tvheadend" : "m3u"
                onIncrement: root.dBackend = root.m3u ? "tvheadend" : "m3u"
                onActivated: root.dBackend = root.m3u ? "tvheadend" : "m3u"
                KeyNavigation.down: hostRow
                Keys.onUpPressed: root.exitUp()
            }
            SettingRow {
                id: hostRow
                width: parent.width
                visible: !root.m3u
                label: qsTr("Address")
                value: root.dHost
                placeholder: qsTr("e.g. 192.168.1.10")
                onActivated: root.editText(label, root.dHost, "text", v => root.dHost = v.trim())
                KeyNavigation.up: sourceRow
                KeyNavigation.down: portRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: portRow
                width: parent.width
                visible: !root.m3u
                label: qsTr("Port")
                value: root.dPort
                onActivated: root.editText(label, String(root.dPort), "number", v => {
                    const p = parseInt(v);
                    if (p > 0 && p < 65536)
                        root.dPort = p;
                })
                KeyNavigation.up: hostRow
                KeyNavigation.down: httpsRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: httpsRow
                width: parent.width
                visible: !root.m3u
                kind: "toggle"
                label: qsTr("Use HTTPS")
                checked: root.dHttps
                onActivated: root.dHttps = !root.dHttps
                KeyNavigation.up: portRow
                KeyNavigation.down: playlistRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: playlistRow
                width: parent.width
                visible: root.m3u
                label: qsTr("Playlist (M3U)")
                value: root.dPlaylist
                placeholder: qsTr("e.g. http://192.168.1.10:9191/output/m3u")
                onActivated: root.editText(label, root.dPlaylist, "text", v => root.dPlaylist = v.trim())
                KeyNavigation.up: httpsRow
                KeyNavigation.down: xmltvRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: xmltvRow
                width: parent.width
                visible: root.m3u
                label: qsTr("Guide (XMLTV)")
                value: root.dXmltv
                placeholder: qsTr("from the playlist")
                onActivated: root.editText(label, root.dXmltv, "text", v => root.dXmltv = v.trim())
                KeyNavigation.up: playlistRow
                KeyNavigation.down: userRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: userRow
                width: parent.width
                label: qsTr("Username")
                value: root.dUser
                placeholder: qsTr("none (anonymous)")
                onActivated: root.editText(label, root.dUser, "text", v => root.dUser = v.trim())
                KeyNavigation.up: xmltvRow
                KeyNavigation.down: passwordRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: passwordRow
                width: parent.width
                label: qsTr("Password")
                value: root.dPassword !== null ? "•".repeat(Math.min(root.dPassword.length, 16)) : (Settings.hasPassword ? "••••••••" : "")
                placeholder: qsTr("not set")
                onActivated: root.editText(label, "", "password", v => root.dPassword = v)
                KeyNavigation.up: userRow
                KeyNavigation.down: profileRow
                KeyNavigation.right: testButton
            }

            Text {
                text: qsTr("Playback & Appearance")
                color: Theme.accent
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 2
                leftPadding: 32
                topPadding: 24
                bottomPadding: 8
            }

            SettingRow {
                id: profileRow
                width: parent.width
                visible: !root.m3u
                label: qsTr("Streaming profile")
                value: root.dProfile
                placeholder: "pass"
                onActivated: root.editText(label, root.dProfile, "text", v => root.dProfile = v.trim().length > 0 ? v.trim() : "pass")
                KeyNavigation.up: passwordRow
                KeyNavigation.down: backgroundRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: backgroundRow
                width: parent.width
                kind: "toggle"
                label: qsTr("Stop playback in background")
                checked: root.dStopInBackground
                onActivated: root.dStopInBackground = !root.dStopInBackground
                KeyNavigation.up: profileRow
                KeyNavigation.down: daysRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: daysRow
                width: parent.width
                kind: "stepper"
                label: qsTr("Days of guide data")
                value: root.dDays === 1 ? qsTr("1 day") : qsTr("%1 days").arg(root.dDays)
                onDecrement: root.dDays = Math.max(1, root.dDays - 1)
                onIncrement: root.dDays = Math.min(14, root.dDays + 1)
                onActivated: root.dDays = root.dDays >= 14 ? 1 : root.dDays + 1
                KeyNavigation.up: backgroundRow
                KeyNavigation.down: schemeRow
                KeyNavigation.right: testButton
            }
            SettingRow {
                id: schemeRow
                width: parent.width
                kind: "stepper"
                label: qsTr("Color scheme")
                value: Settings.colorSchemes[root.schemeIndex].name
                onDecrement: root.cycleScheme(-1)
                onIncrement: root.cycleScheme(1)
                onActivated: root.cycleScheme(1)
                KeyNavigation.up: daysRow
                KeyNavigation.down: testButton
                KeyNavigation.right: testButton
            }
        }
    }

    // --- Status & actions --------------------------------------------
    Column {
        id: side
        anchors.left: formCard.right
        anchors.leftMargin: 48
        anchors.right: parent.right
        anchors.rightMargin: Theme.margin
        y: 16
        spacing: 22

        Rectangle {
            width: parent.width
            height: statusColumn.implicitHeight + 56
            radius: Theme.radius + 8
            color: Theme.card
            border.color: Theme.outline

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 28
                spacing: 12

                Row {
                    spacing: 14
                    Rectangle {
                        width: 18
                        height: 18
                        radius: 9
                        anchors.verticalCenter: parent.verticalCenter
                        color: Tvh.status === Tvh.Online ? Theme.success : Tvh.status === Tvh.Offline ? Theme.danger : Theme.warning
                    }
                    Text {
                        text: Tvh.statusText
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                        width: side.width - 90
                        elide: Text.ElideRight
                    }
                }
                Text {
                    visible: Tvh.serverVersion.length > 0
                    text: Tvh.serverName + " " + Tvh.serverVersion
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    text: Tvh.lastUpdate.getTime() > 0 ? qsTr("Guide updated %1 · %2 channels").arg(Qt.formatDateTime(Tvh.lastUpdate, "dd.MM. HH:mm")).arg(Tvh.channels.count) : qsTr("Guide not loaded yet")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    width: parent.width
                    visible: Settings.secretStoreStatus.length > 0
                    text: Settings.secretStoreStatus
                    color: Theme.warning
                    font.pixelSize: Theme.fontTiny
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    visible: root.testMessage.length > 0 || root.testing
                    text: root.testing ? qsTr("Testing connection…") : (root.testOk ? "✓ " : "✗ ") + root.testMessage
                    color: root.testing ? Theme.textDim : (root.testOk ? Theme.success : Theme.danger)
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
            }
        }

        TvButton {
            id: testButton
            width: parent.width
            text: qsTr("Test connection")
            iconName: "network-connect"
            available: root.hasSource && !root.testing
            onClicked: {
                root.testing = true;
                root.testMessage = "";
                Tvh.testConnection(root.values());
            }
            KeyNavigation.left: sourceRow
            KeyNavigation.down: saveButton
            Keys.onUpPressed: root.exitUp()
        }
        TvButton {
            id: saveButton
            width: parent.width
            primary: true
            text: root.dirty ? qsTr("Save & connect") : qsTr("Saved")
            iconName: root.dirty ? "document-save" : "dialog-ok"
            available: root.dirty && root.hasSource
            onClicked: root.save()
            KeyNavigation.left: sourceRow
            KeyNavigation.up: testButton
            KeyNavigation.down: refreshButton
        }
        TvButton {
            id: refreshButton
            width: parent.width
            text: qsTr("Reload channels & guide")
            iconName: "view-refresh"
            available: Settings.configured && !Tvh.busy
            onClicked: Tvh.refresh()
            KeyNavigation.left: sourceRow
            KeyNavigation.up: saveButton
            KeyNavigation.down: clearButton
        }
        TvButton {
            id: clearButton
            width: parent.width
            text: qsTr("Clear guide cache")
            iconName: "edit-clear-all"
            onClicked: {
                Tvh.clearCache();
                Tvh.refresh();
            }
            KeyNavigation.left: sourceRow
            KeyNavigation.up: refreshButton
            KeyNavigation.down: quitButton
        }
        TvButton {
            id: quitButton
            width: parent.width
            destructive: true
            text: qsTr("Quit application")
            iconName: "application-exit"
            onClicked: Qt.quit()
            KeyNavigation.left: sourceRow
            KeyNavigation.up: clearButton
        }

        Text {
            width: parent.width
            topPadding: 8
            text: qsTr("Remote: arrows to move, OK to select, BACK to return, MENU for settings, GUIDE for the TV guide.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontTiny
            wrapMode: Text.Wrap
        }
    }
}
