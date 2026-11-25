import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Aegisub 1.0

Item {
    id: root
    component AudioTool: CompactIconButton {
        implicitWidth: 28
        implicitHeight: 28
        Layout.alignment: Qt.AlignVCenter
        enabled: audioVM.hasAudio
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 1
        AudioWaveform {
            Accessible.role: Accessible.Graphic
            Accessible.name: i18n.t("Audio waveform")
            Layout.fillWidth: true
            Layout.fillHeight: true
            viewModel: audioVM
            colors: ({ background: themeVM.surface, surface: themeVM.surfaceContainerHigh,
                foreground: themeVM.onSurface, muted: themeVM.onSurfaceVariant,
                primary: themeVM.primary, secondary: themeVM.secondary,
                outline: themeVM.outlineVariant, start: themeVM.danger,
                end: themeVM.endLine,
                keyframe: themeVM.kfLine })
        }
        Rectangle {
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
                AudioTool {
                    icon.source: "qrc:/icons/play_arrow.svg"
                    text: i18n.t("Play Line")
                    onClicked: audioVM.playSelection()
                }
                AudioTool {
                    icon.source: "qrc:/icons/stop.svg"
                    text: i18n.t("Stop")
                    enabled: audioVM.hasAudio && audioVM.isPlaying
                    onClicked: audioVM.stop()
                }
                AudioTool {
                    icon.source: "qrc:/icons/zoom_out.svg"
                    text: i18n.t("Zoom Out")
                    onClicked: audioVM.zoomBy(0.8)
                }
                AudioTool {
                    icon.source: "qrc:/icons/zoom_in.svg"
                    text: i18n.t("Zoom In")
                    onClicked: audioVM.zoomBy(1.25)
                }
                ButtonGroup { id: displayModes }
                AudioTool {
                    icon.source: "qrc:/icons/graphic_eq.svg"
                    text: i18n.t("Wave")
                    checkable: true
                    checked: !audioVM.spectrumMode
                    ButtonGroup.group: displayModes
                    onClicked: audioVM.spectrumMode = false
                }
                AudioTool {
                    icon.source: "qrc:/icons/equalizer.svg"
                    text: i18n.t("Spec")
                    checkable: true
                    checked: audioVM.spectrumMode
                    ButtonGroup.group: displayModes
                    onClicked: audioVM.spectrumMode = true
                }
                AudioTool {
                    icon.source: "qrc:/icons/follow_playhead.svg"
                    text: i18n.t("Follow Playhead")
                    checkable: true
                    checked: audioVM.followPlayhead
                    onToggled: audioVM.followPlayhead = checked
                }
                CheckBox {
                    Layout.alignment: Qt.AlignVCenter
                    implicitHeight: 28
                    text: i18n.t("Snap KF")
                    font.pixelSize: 10
                    spacing: 2
                    padding: 0
                    topPadding: 0
                    bottomPadding: 0
                    topInset: 0
                    bottomInset: 0
                    checked: audioVM.snapToKeyframes
                    enabled: audioVM.keyframeCount > 0
                    onToggled: audioVM.snapToKeyframes = checked
                }
                Label {
                    Layout.alignment: Qt.AlignVCenter
                    text: audioVM.formatTime(audioVM.activeStartMs) + " \u2192 " + audioVM.formatTime(audioVM.activeEndMs)
                    font.pixelSize: 10
                    font.family: "Consolas"
                    color: themeVM.onSurface
                    visible: root.width > 700
                }
                Label {
                    Layout.alignment: Qt.AlignVCenter
                    objectName: "audioTrackLabel"
                    text: audioVM.trackLabel
                    font.pixelSize: 10
                    color: themeVM.secondary
                    elide: Text.ElideRight
                    Layout.maximumWidth: 120
                    visible: audioVM.trackLabel.length > 0 && root.width > 620
                }
                Label {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: root.width > 420 ? 8 : 0
                    text: root.width > 420 ? audioVM.playerStatus : ""
                    font.pixelSize: 10
                    color: themeVM.onSurfaceVariant
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Slider {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumHeight: 28
                    Layout.preferredWidth: root.width > 420 ? 80 : 42
                    Layout.minimumWidth: 30
                    from: 0; to: 1
                    value: audioVM.volume
                    Accessible.name: i18n.t("Volume")
                    enabled: audioVM.hasAudio
                    onMoved: audioVM.volume = value
                }
            }
        }
    }
}
