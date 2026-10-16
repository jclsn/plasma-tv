// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI
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

    function setScheme(id) {
        dScheme = id;
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

    // Keeps the focused row in view; the form is taller than the page.
    function ensureVisible(item) {
        let parent = item;

        while (parent && parent !== form)
            parent = parent.parent;

        if (!parent)
            return;

        const top = item.mapToItem(formCard, 0, 0).y - 16;
        const bottom = top + item.height + 32;

        // Near the top, show the section header too.
        if (top < formView.contentY)
            formView.contentY = top < item.height ? 0 : top;
        else if (bottom > formView.contentY + formView.height)
            formView.contentY = Math.min(formView.contentHeight - formView.height, bottom - formView.height);
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            root.ensureVisible(root.Window.activeFocusItem);
        }
    }

    // --- Form ---------------------------------------------------------
    Flickable {
        id: formView
        x: Theme.margin
        y: 11
        width: 720
        height: parent.height - y - 32
        contentHeight: formCard.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Behavior on contentY {
            NumberAnimation {
                duration: Theme.animNormal
            }
        }

        Rectangle {
            id: formCard
            width: parent.width
            height: form.implicitHeight + 32
            radius: Theme.radius + 5
            color: Theme.card
            border.color: Theme.outline

            Column {
                id: form
                anchors.fill: parent
                anchors.margins: 16
                spacing: 3

                Text {
                    text: qsTr("Server")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    leftPadding: 16
                    topPadding: 5
                    bottomPadding: 5
                }

                BigscreenUI.ComboBoxDelegate {
                    id: sourceRow
                    width: parent.width
                    focus: true
                    text: qsTr("Source")
                    model: [qsTr("TVHeadend"), qsTr("M3U + XMLTV")]
                    currentIndex: root.m3u ? 1 : 0

                    onActivated: index => {
                        root.dBackend = index === 1 ? "m3u" : "tvheadend";
                        currentIndex = Qt.binding(() => root.m3u ? 1 : 0);
                    }

                    KeyNavigation.down: hostRow
                    KeyNavigation.right: testButton
                    Keys.onUpPressed: root.exitUp()
                }
                BigscreenUI.ButtonDelegate {
                    id: hostRow
                    width: parent.width
                    visible: !root.m3u
                    text: qsTr("Address")
                    description: root.dHost.length > 0 ? root.dHost : qsTr("e.g. 192.168.1.10")
                    trailing: EditIcon {}
                    onClicked: root.editText(text, root.dHost, "text", v => root.dHost = v.trim())
                    KeyNavigation.up: sourceRow
                    KeyNavigation.down: portRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.ButtonDelegate {
                    id: portRow
                    width: parent.width
                    visible: !root.m3u
                    text: qsTr("Port")
                    description: root.dPort
                    trailing: EditIcon {}

                    onClicked: root.editText(text, String(root.dPort), "number", v => {
                        const p = parseInt(v);

                        if (p > 0 && p < 65536)
                            root.dPort = p;
                    })

                    KeyNavigation.up: hostRow
                    KeyNavigation.down: httpsRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.SwitchDelegate {
                    id: httpsRow
                    width: parent.width
                    visible: !root.m3u
                    text: qsTr("Use HTTPS")
                    checked: root.dHttps

                    onToggled: {
                        root.dHttps = checked;
                        checked = Qt.binding(() => root.dHttps);
                    }

                    KeyNavigation.up: portRow
                    KeyNavigation.down: playlistRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.ButtonDelegate {
                    id: playlistRow
                    width: parent.width
                    visible: root.m3u
                    text: qsTr("Playlist (M3U)")
                    description: root.dPlaylist.length > 0 ? root.dPlaylist : qsTr("e.g. http://192.168.1.10:9191/output/m3u")
                    trailing: EditIcon {}
                    onClicked: root.editText(text, root.dPlaylist, "text", v => root.dPlaylist = v.trim())
                    KeyNavigation.up: httpsRow
                    KeyNavigation.down: xmltvRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.ButtonDelegate {
                    id: xmltvRow
                    width: parent.width
                    visible: root.m3u
                    text: qsTr("Guide (XMLTV)")
                    description: root.dXmltv.length > 0 ? root.dXmltv : qsTr("from the playlist")
                    trailing: EditIcon {}
                    onClicked: root.editText(text, root.dXmltv, "text", v => root.dXmltv = v.trim())
                    KeyNavigation.up: playlistRow
                    KeyNavigation.down: userRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.ButtonDelegate {
                    id: userRow
                    width: parent.width
                    text: qsTr("Username")
                    description: root.dUser.length > 0 ? root.dUser : qsTr("none (anonymous)")
                    trailing: EditIcon {}
                    onClicked: root.editText(text, root.dUser, "text", v => root.dUser = v.trim())
                    KeyNavigation.up: xmltvRow
                    KeyNavigation.down: passwordRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.ButtonDelegate {
                    id: passwordRow
                    width: parent.width
                    text: qsTr("Password")
                    description: root.dPassword !== null ? "•".repeat(Math.min(root.dPassword.length, 16)) : (Settings.hasPassword ? "••••••••" : qsTr("not set"))
                    trailing: EditIcon {}
                    onClicked: root.editText(text, "", "password", v => root.dPassword = v)
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
                    font.letterSpacing: 1
                    leftPadding: 16
                    topPadding: 16
                    bottomPadding: 5
                }

                BigscreenUI.ButtonDelegate {
                    id: profileRow
                    width: parent.width
                    visible: !root.m3u
                    text: qsTr("Streaming profile")
                    description: root.dProfile.length > 0 ? root.dProfile : "pass"
                    trailing: EditIcon {}
                    onClicked: root.editText(text, root.dProfile, "text", v => root.dProfile = v.trim().length > 0 ? v.trim() : "pass")
                    KeyNavigation.up: passwordRow
                    KeyNavigation.down: backgroundRow
                    KeyNavigation.right: testButton
                }
                BigscreenUI.SwitchDelegate {
                    id: backgroundRow
                    width: parent.width
                    text: qsTr("Stop playback in background")
                    checked: root.dStopInBackground

                    onToggled: {
                        root.dStopInBackground = checked;
                        checked = Qt.binding(() => root.dStopInBackground);
                    }

                    KeyNavigation.up: profileRow
                    KeyNavigation.down: daysRow
                    KeyNavigation.right: testButton
                }
                // Left/right change the value; too many choices for a ComboBoxDelegate.
                BigscreenUI.ButtonDelegate {
                    id: daysRow
                    width: parent.width
                    text: qsTr("Days of guide data")
                    description: root.dDays === 1 ? qsTr("1 day") : qsTr("%1 days").arg(root.dDays)
                    trailing: Row {
                        spacing: 5
                        Kirigami.Icon {
                            source: "go-previous"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: root.dDays > 1 ? 1 : 0.3
                        }
                        Kirigami.Icon {
                            source: "go-next"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: root.dDays < 14 ? 1 : 0.3
                        }
                    }
                    onClicked: root.dDays = root.dDays >= 14 ? 1 : root.dDays + 1
                    Keys.onLeftPressed: root.dDays = Math.max(1, root.dDays - 1)
                    Keys.onRightPressed: root.dDays = Math.min(14, root.dDays + 1)
                    KeyNavigation.up: backgroundRow
                    KeyNavigation.down: schemeRow
                }
                BigscreenUI.ComboBoxDelegate {
                    id: schemeRow
                    width: parent.width
                    text: qsTr("Color scheme")
                    model: Settings.colorSchemes.map(scheme => scheme.name)
                    currentIndex: root.schemeIndex

                    // The option dialog grows with the options; keep it on the screen.
                    Component.onCompleted: {
                        for (const object of data) {
                            if (object instanceof T.Dialog)
                                object.height = Qt.binding(() => Math.min(object.implicitHeight, object.parent.height * 0.9));
                        }
                    }

                    onActivated: index => {
                        root.setScheme(Settings.colorSchemes[index].id);
                        currentIndex = Qt.binding(() => root.schemeIndex);
                    }

                    KeyNavigation.up: daysRow
                    KeyNavigation.down: testButton
                    KeyNavigation.right: testButton
                }
            }
        }
    }

    component EditIcon: Kirigami.Icon {
        source: "document-edit"
        implicitWidth: Kirigami.Units.iconSizes.smallMedium
        implicitHeight: Kirigami.Units.iconSizes.smallMedium
    }

    // --- Status & actions --------------------------------------------
    Column {
        id: side
        anchors.left: formView.right
        anchors.leftMargin: 32
        anchors.right: parent.right
        anchors.rightMargin: Theme.margin
        y: 11
        spacing: 15

        Rectangle {
            width: parent.width
            height: statusColumn.implicitHeight + 37
            radius: Theme.radius + 5
            color: Theme.card
            border.color: Theme.outline

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 19
                spacing: 8

                Row {
                    spacing: 9
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: Tvh.status === Tvh.Online ? Theme.success : Tvh.status === Tvh.Offline ? Theme.danger : Theme.warning
                    }
                    Text {
                        text: Tvh.statusText
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                        width: side.width - 60
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
            icon.name: "network-connect"
            available: root.hasSource && !root.testing
            onActivated: {
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
            text: root.dirty ? qsTr("Save & connect") : qsTr("Saved")
            icon.name: root.dirty ? "document-save" : "dialog-ok"
            available: root.dirty && root.hasSource
            onActivated: root.save()
            KeyNavigation.left: sourceRow
            KeyNavigation.up: testButton
            KeyNavigation.down: refreshButton
        }
        TvButton {
            id: refreshButton
            width: parent.width
            text: qsTr("Reload channels & guide")
            icon.name: "view-refresh"
            available: Settings.configured && !Tvh.busy
            onActivated: Tvh.refresh()
            KeyNavigation.left: sourceRow
            KeyNavigation.up: saveButton
            KeyNavigation.down: clearButton
        }
        TvButton {
            id: clearButton
            width: parent.width
            text: qsTr("Clear guide cache")
            icon.name: "edit-clear-all"
            onActivated: {
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
            text: qsTr("Quit application")
            icon.name: "application-exit"
            onActivated: Qt.quit()
            KeyNavigation.left: sourceRow
            KeyNavigation.up: clearButton
        }

        Text {
            width: parent.width
            topPadding: 5
            text: qsTr("Remote: arrows to move, OK to select, BACK to return, MENU for settings, GUIDE for the TV guide.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontTiny
            wrapMode: Text.Wrap
        }
    }
}
