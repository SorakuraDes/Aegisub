import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Aegisub 1.0

// Video preview panel: the decoded frame with the libass overlay on top and a
// compact transport bar below. The view model owns decoding/playback state; this
// file only maps it to controls.
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 1

        VideoViewport {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "#1b1b1b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 4

                Button {
                    text: videoVM.isPlaying ? i18n.t("Pause") : i18n.t("Play")
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8
                    onClicked: videoVM.togglePlay()
                }

                Button {
                    text: "\u25C0"
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 6
                    rightPadding: 6
                    onClicked: videoVM.stepFrame(-1)
                }

                Button {
                    text: "\u25B6"
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 6
                    rightPadding: 6
                    onClicked: videoVM.stepFrame(1)
                }

                Slider {
                    id: seekSlider
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    from: 0
                    to: Math.max(0, videoVM.frameCount - 1)
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    // Follow the view model unless the user is dragging.
                    Binding {
                        target: seekSlider
                        property: "value"
                        value: videoVM.currentFrame
                        when: !seekSlider.pressed
                    }
                    onMoved: videoVM.seekFrame(value)
                }

                Label {
                    text: "Frame " + (videoVM.currentFrame + 1) + " / " + videoVM.frameCount
                          + "   " + videoVM.currentTimeText
                    font.pixelSize: 11
                    font.family: "Consolas"
                    color: "#e0e0e0"
                    Layout.preferredWidth: 210
                    horizontalAlignment: Text.AlignRight
                }

                CheckBox {
                    text: i18n.t("Sync")
                    checked: videoVM.autoSeekToActiveLine
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    onToggled: videoVM.autoSeekToActiveLine = checked
                }
            }
        }
    }

    // The view model pushes fully composited frames; no image URL/provider.
    Connections {
        target: videoVM
        function onFrameReady(frame) {
            viewport.frame = frame
        }
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

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 38
        anchors.margins: 6
        visible: videoVM.lastError.length > 0
        text: videoVM.lastError
        color: "#ff9f9f"
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
}
