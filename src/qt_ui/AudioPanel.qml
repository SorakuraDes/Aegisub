import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Aegisub 1.0

// Audio waveform panel: interactive waveform (drag / left click = Start,
// right click = End, Ctrl+wheel = zoom, wheel = scroll) plus a compact
// transport bar. The view model owns decoding, playback and the timing commit.
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 1

        AudioWaveform {
            id: waveform
            Layout.fillWidth: true
            Layout.fillHeight: true
            viewModel: audioVM
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
                    text: i18n.t("Play Line")
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8
                    enabled: audioVM.hasAudio
                    onClicked: audioVM.playSelection()
                }

                Button {
                    text: i18n.t("Stop")
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 6
                    rightPadding: 6
                    enabled: audioVM.hasAudio && audioVM.isPlaying
                    onClicked: audioVM.stop()
                }

                Button {
                    text: "\u2212"
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 6
                    rightPadding: 6
                    enabled: audioVM.hasAudio
                    onClicked: audioVM.zoomBy(0.8)
                }

                Button {
                    text: "+"
                    flat: true
                    font.pixelSize: 11
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 6
                    rightPadding: 6
                    enabled: audioVM.hasAudio
                    onClicked: audioVM.zoomBy(1.25)
                }

                CheckBox {
                    text: i18n.t("Snap KF")
                    font.pixelSize: 9
                    spacing: 2
                    implicitHeight: 24
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 0
                    rightPadding: 0
                    checked: audioVM.snapToKeyframes
                    enabled: audioVM.keyframeCount > 0
                    onToggled: audioVM.snapToKeyframes = checked
                }

                Label {
                    text: audioVM.formatTime(audioVM.activeStartMs) + " \u2192 "
                          + audioVM.formatTime(audioVM.activeEndMs)
                    font.pixelSize: 10
                    font.family: "Consolas"
                    color: "#e0e0e0"
                    elide: Text.ElideRight
                    Layout.preferredWidth: 150
                    // Responsive: the dual-panel layout is too narrow for the
                    // time range; keep the track/player status readable there.
                    visible: root.width > 700
                }

                Label {
                    objectName: "audioTrackLabel"
                    text: audioVM.trackLabel
                    font.pixelSize: 10
                    color: "#b388ff"
                    elide: Text.ElideRight
                    Layout.maximumWidth: 120
                    visible: audioVM.trackLabel.length > 0 && root.width > 620
                }

                Label {
                    text: audioVM.playerStatus
                    font.pixelSize: 10
                    color: "#8a8a8a"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: 180
                    visible: audioVM.hasAudio && audioVM.playerStatus.length > 0
                }

                Slider {
                    Layout.preferredWidth: 80
                    from: 0
                    to: 1
                    value: audioVM.volume
                    enabled: audioVM.hasAudio
                    onMoved: audioVM.volume = value
                }
            }
        }
    }
}
