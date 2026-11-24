import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Aegisub 1.0

Item {
    id: root

    component TransportButton: Button {
        flat: true
        font.pixelSize: 11
        implicitHeight: 24
        padding: 0
        leftInset: 0
        rightInset: 0
        topInset: 0
        bottomInset: 0
    }

    component ToolButton: TransportButton {
        id: button
        implicitWidth: 32
        implicitHeight: 29
        contentItem: Text {
            text: button.text
            font: button.font
            color: button.checked ? "#4fc3f7" : "#eeeeee"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 3
            color: button.down ? "#38464d" : button.checked ? "#263a45"
                   : button.hovered ? "#303030" : "transparent"
            border.color: button.activeFocus ? "#4fc3f7" : "transparent"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 1

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 36
                Layout.fillHeight: true
                visible: videoVM.hasVideo
                color: "#1b1b1b"

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 3
                    spacing: 2
                    ButtonGroup { id: toolGroup }
                    ToolButton {
                        text: "Std"
                        objectName: "visualStandardButton"
                        checkable: true
                        ButtonGroup.group: toolGroup
                        checked: visualToolsVM.activeTool === 0
                        onClicked: visualToolsVM.activeTool = 0
                    }
                    ToolButton {
                        text: "Pos"
                        objectName: "visualPositionButton"
                        checkable: true
                        ButtonGroup.group: toolGroup
                        checked: visualToolsVM.activeTool === 1
                        onClicked: visualToolsVM.activeTool = 1
                    }
                    ToolButton {
                        text: "Clip"
                        objectName: "visualClipButton"
                        checkable: true
                        ButtonGroup.group: toolGroup
                        checked: visualToolsVM.activeTool === 2
                        onClicked: visualToolsVM.activeTool = 2
                    }
                    ToolButton {
                        text: "\u00d7"
                        objectName: "visualClearClipButton"
                        Accessible.name: "Clear rectangular clip"
                        font.pixelSize: 17
                        visible: visualToolsVM.activeTool === 2 && visualToolsVM.hasRectClip
                        onClicked: visualToolsVM.clearRectClip()
                    }
                }
            }

            VideoViewport {
                id: videoSurface
                Layout.fillWidth: true
                Layout.fillHeight: true

                VisualOverlay {
                    anchors.fill: parent
                    viewport: videoSurface
                    viewModel: visualToolsVM
                }

                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 6
                    text: videoVM.videoName
                    color: "#d0d0d0"
                    font.pixelSize: 10
                    style: Text.Outline
                    styleColor: "#000000"
                    visible: videoVM.hasVideo
                }
            }
        }

        Rectangle {
            id: transport
            readonly property bool narrow: width < 500
            readonly property bool compactFrame: width < 390
            Layout.fillWidth: true
            Layout.preferredHeight: narrow ? 46 : 34
            color: "#1b1b1b"

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                height: 26
                spacing: 3

                TransportButton {
                    text: videoVM.isPlaying ? i18n.t("Pause") : i18n.t("Play")
                    implicitWidth: Math.max(41, contentItem.implicitWidth + 6)
                    onClicked: videoVM.togglePlay()
                }
                TransportButton {
                    text: "\u25C0"
                    implicitWidth: 23
                    onClicked: videoVM.stepFrame(-1)
                }
                TransportButton {
                    text: "\u25B6"
                    implicitWidth: 23
                    onClicked: videoVM.stepFrame(1)
                }

                Slider {
                    id: seekSlider
                    Layout.fillWidth: true
                    Layout.minimumWidth: 32
                    leftPadding: 4
                    rightPadding: 4
                    from: 0
                    to: Math.max(0, videoVM.frameCount - 1)
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    Binding {
                        target: seekSlider
                        property: "value"
                        value: videoVM.currentFrame
                        when: !seekSlider.pressed
                    }
                    onMoved: videoVM.seekFrame(value)
                }

                Label {
                    text: videoVM.currentFrameIsKeyframe ? "KF" : ""
                    Layout.preferredWidth: 16
                    font.pixelSize: 10
                    font.bold: true
                    color: "#b388ff"
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    readonly property string frameText: "Frame " + (videoVM.currentFrame + 1) + " / " + videoVM.frameCount
                    text: frameText + (transport.compactFrame ? "\n" : "   ") + videoVM.currentTimeText
                    Accessible.name: frameText + "   " + videoVM.currentTimeText
                    font.pixelSize: 11
                    font.family: "Consolas"
                    color: "#e0e0e0"
                    horizontalAlignment: Text.AlignRight
                }

                Label {
                    visible: !transport.narrow
                    text: visible ? visualToolsVM.hoverCoordText : ""
                    Layout.preferredWidth: 74
                    elide: Text.ElideRight
                    font.pixelSize: 10
                    font.family: "Consolas"
                    color: "#4fc3f7"
                    horizontalAlignment: Text.AlignRight
                }

                CheckBox {
                    text: i18n.t("Sync")
                    checked: videoVM.autoSeekToActiveLine
                    font.pixelSize: 11
                    implicitHeight: 24
                    padding: 0
                    spacing: 1
                    onToggled: videoVM.autoSeekToActiveLine = checked
                }
            }

            Label {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 4
                visible: transport.narrow
                text: visible ? visualToolsVM.hoverCoordText : ""
                font.pixelSize: 10
                font.family: "Consolas"
                color: "#4fc3f7"
            }
        }
    }

    Connections {
        target: videoVM
        function onFrameReady(frame) { videoSurface.frame = frame }
    }

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: transport.height + 4
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        visible: videoVM.lastError.length > 0
        text: videoVM.lastError
        color: "#ff9f9f"
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
}
