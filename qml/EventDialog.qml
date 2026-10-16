// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import PlasmaTv

// Programme details with Watch / Record actions.
FocusScope {
    id: root
    property var event: ({})
    property var channel: ({})
    property Item returnFocus: null
    readonly property bool airing: event.start <= Tvh.now && event.stop > Tvh.now
    readonly property bool hasRecording: (event.dvrState ?? "").length > 0
    signal watch(string uuid)

    visible: false
    z: 90

    function open(ev) {
        event = ev;
        channel = Tvh.channels.get(Tvh.channels.indexOfUuid(ev.channel));
        returnFocus = Window.activeFocusItem;
        description.contentY = 0;
        visible = true;
        if (airing)
            watchButton.forceActiveFocus();
        else if (Tvh.canRecord)
            recordButton.forceActiveFocus();
        else
            closeButton.forceActiveFocus();
    }

    function close() {
        visible = false;
        if (returnFocus)
            returnFocus.forceActiveFocus();
    }

    Connections {
        target: Tvh
        enabled: root.visible
        function onEpgChanged() {
            const updated = Tvh.eventById(root.event.id);
            if (updated.id !== undefined)
                root.event = updated;
        }
    }

    Keys.onPressed: event => {
        if (Theme.isBack(event.key)) {
            close();
        } else if (event.key === Qt.Key_Up) {
            description.contentY = Math.max(0, description.contentY - 120);
        } else if (event.key === Qt.Key_Down) {
            description.contentY = Math.max(0, Math.min(description.contentHeight - description.height, description.contentY + 120));
        } else if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            // don't let digits reach the global channel number entry
        } else {
            return;
        }
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.72)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 1400
        height: 800
        radius: Theme.radius + 12
        color: Theme.surface
        border.color: Theme.outline
        clip: true
        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            width: parent.width
            height: 10
            color: Theme.genreColor(root.event.genre ?? 0)
        }

        Row {
            id: top
            x: 56
            y: 56
            spacing: 28
            ChannelLogo {
                width: 160
                height: 96
                source: root.channel.iconUrl ?? ""
                name: root.channel.name ?? ""
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    text: root.channel.name ?? ""
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 2
                }
                Row {
                    spacing: 16
                    Text {
                        text: root.event.start ? Theme.formatDay(root.event.start) + "  ·  " + Theme.formatRange(root.event.start, root.event.stop) + "  ·  " + Theme.formatDuration(root.event.stop - root.event.start) : ""
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                    }
                    Rectangle {
                        visible: (root.event.genreName ?? "").length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: g.implicitWidth + 24
                        height: g.implicitHeight + 6
                        radius: height / 2
                        color: Theme.genreColor(root.event.genre ?? 0)
                        Text {
                            id: g
                            anchors.centerIn: parent
                            text: root.event.genreName ?? ""
                            color: "white"
                            font.pixelSize: Theme.fontTiny
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        visible: root.hasRecording
                        text: root.event.dvrState === "recording" ? qsTr("● Recording") : qsTr("● Scheduled")
                        color: Theme.recording
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Text {
            id: title
            anchors.top: top.bottom
            anchors.topMargin: 36
            x: 56
            width: parent.width - 112
            text: root.event.title ?? ""
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            id: subtitle
            anchors.top: title.bottom
            anchors.topMargin: 8
            x: 56
            width: parent.width - 112
            visible: text.length > 0
            text: root.event.subtitle ?? ""
            color: Theme.textDim
            font.pixelSize: Theme.fontLarge
            elide: Text.ElideRight
        }

        ThinProgress {
            id: progress
            anchors.top: subtitle.visible ? subtitle.bottom : title.bottom
            anchors.topMargin: 20
            x: 56
            width: parent.width - 112
            visible: root.airing
            value: root.airing ? (Tvh.now - root.event.start) / (root.event.stop - root.event.start) : 0
        }

        Flickable {
            id: description
            anchors.top: progress.bottom
            anchors.topMargin: 24
            anchors.bottom: buttons.top
            anchors.bottomMargin: 32
            x: 56
            width: parent.width - 112
            contentHeight: descText.implicitHeight
            clip: true
            Behavior on contentY {
                NumberAnimation {
                    duration: Theme.animNormal
                }
            }

            Text {
                id: descText
                width: parent.width
                text: {
                    const s = root.event.summary ?? "";
                    const d = root.event.description ?? "";
                    if (s.length > 0 && d.length > 0 && !d.startsWith(s))
                        return s + "\n\n" + d;
                    return d.length > 0 ? d : (s.length > 0 ? s : qsTr("No description available."));
                }
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }
        }

        Row {
            id: buttons
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            x: 56
            spacing: 24

            TvButton {
                id: watchButton
                visible: root.airing
                primary: true
                text: qsTr("Watch now")
                iconName: "media-playback-start"
                onClicked: {
                    root.close();
                    root.watch(root.event.channel);
                }
                KeyNavigation.right: recordButton
            }
            TvButton {
                id: recordButton
                visible: Tvh.canRecord
                text: !root.hasRecording ? qsTr("Record") : root.event.dvrState === "recording" ? qsTr("Stop recording") : qsTr("Cancel recording")
                iconName: root.hasRecording ? "media-playback-stop" : "media-record"
                destructive: root.hasRecording
                onClicked: {
                    if (root.hasRecording)
                        Tvh.cancelRecording(root.event.id);
                    else
                        Tvh.record(root.event.id);
                }
                KeyNavigation.left: root.airing ? watchButton : null
                KeyNavigation.right: closeButton
            }
            TvButton {
                id: closeButton
                text: qsTr("Close")
                onClicked: root.close()
                KeyNavigation.left: recordButton
            }
        }
    }
}
