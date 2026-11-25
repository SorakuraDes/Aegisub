import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

/// Qt replacement for the wxWidgets "choose track" dialog of the ffms2
/// providers: shown (modally) when a media file has more than one video or
/// audio track. The view model probes the container first and only then asks
/// this window; the chosen ffms2 track id goes back through
/// openLocalPathWithTrack(), so the providers themselves never prompt.
Window {
    id: root
    width: 380
    height: 264
    minimumWidth: 320
    minimumHeight: 210
    color: themeVM.surface
    modality: Qt.ApplicationModal
    title: root.mediaKind === "video" ? i18n.t("Select Video Track") : i18n.t("Select Audio Track")

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    property string mediaPath: ""
    property string mediaKind: "audio"
    property var trackItems: []
    property int selectedTrackId: -1
    /// Set right before confirm() closes the window so onClosing does not treat
    /// it as a cancel.
    property bool closingForConfirm: false

    function openFor(path, kind, tracks) {
        mediaPath = path
        mediaKind = kind
        trackItems = tracks
        selectedTrackId = tracks.length > 0 ? tracks[0].id : -1
        listView.currentIndex = tracks.length > 0 ? 0 : -1
        closingForConfirm = false
        show()
        raise()
        requestActivate()
    }

    function confirm() {
        if (selectedTrackId < 0)
            return
        var path = mediaPath
        var kind = mediaKind
        var trackId = selectedTrackId
        closingForConfirm = true
        close()
        if (kind === "video")
            videoVM.openLocalPathWithTrack(path, trackId)
        else
            audioVM.openLocalPathWithTrack(path, trackId)
    }

    function cancel() {
        var kind = mediaKind
        closingForConfirm = true
        close()
        if (kind === "video")
            videoVM.cancelPendingTrackSelection()
        else
            audioVM.cancelPendingTrackSelection()
    }

    onClosing: {
        if (!closingForConfirm) {
            if (mediaKind === "video")
                videoVM.cancelPendingTrackSelection()
            else
                audioVM.cancelPendingTrackSelection()
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: root.cancel()
    }

    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        onActivated: root.confirm()
    }

    ButtonGroup { id: trackGroup }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: root.mediaKind === "video"
                  ? i18n.t("Multiple video tracks detected, please choose the one you wish to load:")
                  : i18n.t("Multiple audio tracks detected, please choose the one you wish to load:")
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            color: themeVM.onSurface
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.trackItems
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: trackRow
                required property int index
                required property var modelData
                width: ListView.view.width
                height: themeVM.controlHeight
                readonly property bool selected: root.selectedTrackId === modelData.id

                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    radius: themeVM.radiusSmall
                    color: trackRow.selected ? themeVM.primaryContainer : "transparent"
                    border.width: radio.visualFocus ? 2 : 0
                    border.color: themeVM.onSurface
                }

                RadioButton {
                    id: radio
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    text: modelData.label !== undefined
                          ? modelData.label
                          : ("Track " + (index + 1) + " \u2014 "
                             + (modelData.description !== undefined ? modelData.description : ""))
                    font.pixelSize: 12
                    topPadding: 0
                    bottomPadding: 0
                    ButtonGroup.group: trackGroup
                    checked: trackRow.selected
                    contentItem: Label {
                        leftPadding: radio.mirrored ? 0 : radio.indicator.width + radio.spacing
                        rightPadding: radio.mirrored ? radio.indicator.width + radio.spacing : 0
                        text: radio.text
                        font: radio.font
                        color: trackRow.selected ? themeVM.onPrimaryContainer : themeVM.onSurface
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: root.selectedTrackId = modelData.id
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onDoubleTapped: {
                        root.selectedTrackId = modelData.id
                        root.confirm()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                objectName: "trackPickerCancelButton"
                text: i18n.t("Cancel")
                onClicked: root.cancel()
            }

            Button {
                objectName: "trackPickerOpenButton"
                text: i18n.t("Open")
                highlighted: true
                enabled: root.selectedTrackId >= 0
                onClicked: root.confirm()
            }
        }
    }
}
