pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Aegisub 1.0

Item {
    id: root

    component TransportButton: CompactIconButton {
        implicitWidth: 28
        implicitHeight: 28
        Layout.alignment: Qt.AlignVCenter
    }

    component VideoTool: CompactIconButton {
        implicitWidth: 28
        implicitHeight: 28
        onActiveFocusChanged: {
            if (activeFocus)
                toolViewport.revealTool(toolColumn.y + y, height)
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
                Layout.minimumWidth: 36
                Layout.preferredWidth: 36
                Layout.maximumWidth: 36
                Layout.fillHeight: true
                visible: videoVM.hasVideo
                color: themeVM.surfaceContainer

                Flickable {
                    id: toolViewport
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: toolColumn.implicitHeight + 6
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height

                    function revealTool(top, toolHeight) {
                        if (!interactive)
                            return
                        if (top < contentY)
                            contentY = top
                        else if (top + toolHeight > contentY + height)
                            contentY = top + toolHeight - height
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        width: 4
                        padding: 0
                    }

                    Column {
                        id: toolColumn
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 3
                        spacing: 2
                        ButtonGroup { id: toolGroup }
                        VideoTool {
                            text: i18n.t("Standard tool")
                            icon.source: "qrc:/icons/near_me.svg"
                            objectName: "visualStandardButton"
                            checkable: true
                            ButtonGroup.group: toolGroup
                            checked: visualToolsVM.activeTool === 0
                            onClicked: visualToolsVM.activeTool = 0
                        }
                        VideoTool {
                            text: i18n.t("Move subtitles")
                            icon.source: "qrc:/icons/open_with.svg"
                            objectName: "visualPositionButton"
                            checkable: true
                            ButtonGroup.group: toolGroup
                            checked: visualToolsVM.activeTool === 1
                            onClicked: visualToolsVM.activeTool = 1
                        }
                        VideoTool {
                            text: i18n.t("Animate subtitle position")
                            icon.source: "qrc:/icons/trending_flat.svg"
                            objectName: "visualMoveButton"
                            checkable: true
                            ButtonGroup.group: toolGroup
                            checked: visualToolsVM.activeTool === 2
                            onClicked: visualToolsVM.activeTool = 2
                        }
                        VideoTool {
                            text: i18n.t("Rotate subtitles around Z axis")
                            icon.source: "qrc:/icons/rotate_right.svg"
                            objectName: "visualRotateZButton"
                            checkable: true
                            ButtonGroup.group: toolGroup
                            checked: visualToolsVM.activeTool === 3
                            onClicked: visualToolsVM.activeTool = 3
                        }
                        VideoTool {
                            text: i18n.t("Scale subtitles")
                            icon.source: "qrc:/icons/aspect_ratio.svg"
                            objectName: "visualScaleButton"
                            checkable: true
                            ButtonGroup.group: toolGroup
                            checked: visualToolsVM.activeTool === 4
                            onClicked: visualToolsVM.activeTool = 4
                        }
                        VideoTool {
                            text: i18n.t("Rectangular clip")
                            icon.source: "qrc:/icons/crop.svg"
                            objectName: "visualClipButton"
                            checkable: true
                            ButtonGroup.group: toolGroup
                            checked: visualToolsVM.activeTool === 5
                            onClicked: visualToolsVM.activeTool = 5
                        }
                        VideoTool {
                            text: i18n.t("Clear rectangular clip")
                            icon.source: "qrc:/icons/close.svg"
                            objectName: "visualClearClipButton"
                            visible: visualToolsVM.activeTool === 5 && visualToolsVM.hasRectClip
                            onClicked: visualToolsVM.clearRectClip()
                        }
                    }
                }
            }

            VideoViewport {
                id: videoSurface
                objectName: "videoViewport"
                zoomPercent: videoVM.zoomPercent
                clip: true
                Accessible.role: Accessible.Graphic
                Accessible.name: i18n.t("Video preview")
                Layout.fillWidth: true
                Layout.fillHeight: true

                VisualOverlay {
                    objectName: "visualOverlay"
                    anchors.fill: parent
                    viewport: videoSurface
                    viewModel: visualToolsVM
                    accentColor: themeVM.primary
                    highlightColor: themeVM.secondary
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    elide: Text.ElideMiddle
                    text: videoVM.videoName
                    // Contrast the glyph and its outline over arbitrary footage.
                    color: themeVM.onSurface
                    font.pixelSize: 10
                    style: Text.Outline
                    styleColor: themeVM.surface
                    visible: videoVM.hasVideo
                }
            }
        }

        Rectangle {
            id: transport
            readonly property bool compactFrame: width < 430
            Layout.fillWidth: true
            Layout.minimumHeight: 36
            Layout.preferredHeight: 36
            Layout.maximumHeight: 36
            color: themeVM.surfaceContainer

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 0
                anchors.bottomMargin: 0
                spacing: 2

                TransportButton {
                    text: videoVM.isPlaying ? i18n.t("Pause") : i18n.t("Play")
                    icon.source: videoVM.isPlaying ? "qrc:/icons/pause.svg" : "qrc:/icons/play_arrow.svg"
                    onClicked: videoVM.togglePlay()
                }
                TransportButton {
                    text: i18n.t("Previous frame")
                    icon.source: "qrc:/icons/skip_previous.svg"
                    onClicked: videoVM.stepFrame(-1)
                }
                TransportButton {
                    text: i18n.t("Next frame")
                    icon.source: "qrc:/icons/skip_next.svg"
                    onClicked: videoVM.stepFrame(1)
                }

                Slider {
                    id: seekSlider
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                    Layout.minimumWidth: 32
                    Layout.maximumHeight: 28
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
                    Layout.alignment: Qt.AlignVCenter
                    text: videoVM.currentFrameIsKeyframe ? "KF" : ""
                    Layout.preferredWidth: 16
                    font.pixelSize: 10
                    font.bold: true
                    color: themeVM.kfLine
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    Layout.alignment: Qt.AlignVCenter
                    readonly property string frameText: "Frame " + (videoVM.currentFrame + 1) + " / " + videoVM.frameCount
                    text: frameText + (transport.compactFrame ? "\n" : "   ") + videoVM.currentTimeText
                    Accessible.name: frameText + "   " + videoVM.currentTimeText
                    font.pixelSize: 11
                    font.family: "Consolas"
                    color: themeVM.onSurface
                    horizontalAlignment: Text.AlignRight
                }

                Label {
                    Layout.alignment: Qt.AlignVCenter
                    visible: transport.width >= 500
                    text: visualToolsVM.hoverCoordText
                    Layout.preferredWidth: 74
                    elide: Text.ElideRight
                    font.pixelSize: 10
                    font.family: "Consolas"
                    color: themeVM.primary
                    horizontalAlignment: Text.AlignRight
                }

                CheckBox {
                    Layout.alignment: Qt.AlignVCenter
                    text: i18n.t("Sync")
                    checked: videoVM.autoSeekToActiveLine
                    font.pixelSize: 11
                    implicitHeight: 28
                    padding: 0
                    topPadding: 0
                    bottomPadding: 0
                    topInset: 0
                    bottomInset: 0
                    spacing: 1
                    onToggled: videoVM.autoSeekToActiveLine = checked
                }
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
        color: themeVM.danger
        background: Rectangle { color: themeVM.surfaceContainer }
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
}
