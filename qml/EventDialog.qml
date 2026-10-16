// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Layouts
import org.kde.bigscreen.ui as BigscreenUI
import PlasmaTv

// Programme details with Watch / Record actions.
BigscreenUI.Dialog {
    id: root
    property var event: ({})
    property var channel: ({})
    property Item returnFocus: null
    readonly property bool airing: event.start <= Tvh.now && event.stop > Tvh.now
    readonly property bool hasRecording: (event.dvrState ?? "").length > 0
    signal watch(string uuid)

    width: 933
    openFocusItem: airing ? watchButton : Tvh.canRecord ? recordButton : closeButton

    function show(ev) {
        event = ev;
        channel = Tvh.channels.get(Tvh.channels.indexOfUuid(ev.channel));
        returnFocus = Window.activeFocusItem;
        description.contentY = 0;

        open();
    }

    onAboutToHide: {
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

    contentItem: ColumnLayout {
        spacing: 11

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 4
            radius: 2
            color: Theme.genreColor(root.event.genre ?? 0)
        }

        RowLayout {
            Layout.topMargin: 11
            spacing: 19
            ChannelLogo {
                implicitWidth: 107
                implicitHeight: 64
                source: root.channel.iconUrl ?? ""
                name: root.channel.name ?? ""
            }
            ColumnLayout {
                spacing: 4
                Text {
                    text: root.channel.name ?? ""
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                }
                RowLayout {
                    spacing: 11
                    BigscreenUI.Label {
                        text: root.event.start ? Theme.formatDay(root.event.start) + "  ·  " + Theme.formatRange(root.event.start, root.event.stop) + "  ·  " + Theme.formatDuration(root.event.stop - root.event.start) : ""
                        color: Theme.textDim
                    }
                    Rectangle {
                        visible: (root.event.genreName ?? "").length > 0
                        implicitWidth: g.implicitWidth + 16
                        implicitHeight: g.implicitHeight + 4
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
                    BigscreenUI.Label {
                        visible: root.hasRecording
                        text: root.event.dvrState === "recording" ? qsTr("● Recording") : qsTr("● Scheduled")
                        color: Theme.recording
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        BigscreenUI.Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: root.event.title ?? ""
            type: BigscreenUI.Label.Title
            font.weight: Font.Normal
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        BigscreenUI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.event.subtitle ?? ""
            type: BigscreenUI.Label.Heading
            color: Theme.textDim
            elide: Text.ElideRight
        }

        BigscreenUI.ProgressBar {
            Layout.fillWidth: true
            visible: root.airing
            value: root.airing ? (Tvh.now - root.event.start) / (root.event.stop - root.event.start) : 0
        }

        Flickable {
            id: description
            Layout.fillWidth: true
            Layout.preferredHeight: 187
            Layout.topMargin: 5
            contentHeight: descText.implicitHeight
            clip: true
            Behavior on contentY {
                NumberAnimation {
                    duration: Theme.animNormal
                }
            }

            BigscreenUI.Label {
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
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            Layout.topMargin: 11
            spacing: 11
            BigscreenUI.KeyNavigationGroup.orientation: Qt.Horizontal

            // Up/down scroll the description; the buttons keep the focus.
            Keys.onUpPressed: description.contentY = Math.max(0, description.contentY - 80)
            Keys.onDownPressed: description.contentY = Math.max(0, Math.min(description.contentHeight - description.height, description.contentY + 80))

            TvButton {
                id: watchButton
                visible: root.airing
                text: qsTr("Watch now")
                icon.name: "media-playback-start"
                onActivated: {
                    root.close();
                    root.watch(root.event.channel);
                }
            }
            TvButton {
                id: recordButton
                visible: Tvh.canRecord
                text: !root.hasRecording ? qsTr("Record") : root.event.dvrState === "recording" ? qsTr("Stop recording") : qsTr("Cancel recording")
                icon.name: root.hasRecording ? "media-playback-stop" : "media-record"
                onActivated: {
                    if (root.hasRecording)
                        Tvh.cancelRecording(root.event.id);
                    else
                        Tvh.record(root.event.id);
                }
            }
            TvButton {
                id: closeButton
                text: qsTr("Close")
                icon.name: "dialog-close"
                onActivated: root.close()
            }
        }
    }
}
