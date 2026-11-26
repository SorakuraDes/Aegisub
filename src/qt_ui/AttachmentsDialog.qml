pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "attachmentsDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    title: i18n.t("Attachments")
    width: Math.max(0, Math.min(680, parent ? parent.width - 32 : 680))
    height: Math.max(0, Math.min(510, parent ? parent.height - 32 : 510))
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    background: Rectangle {
        objectName: "attachmentsBackground"
        color: themeVM.surfaceContainerHigh
        radius: 8
        border.color: themeVM.outlineVariant
        border.width: 1
    }

    property var selectedRows: []
    property var pendingRows: []
    readonly property int sizeColumnWidth: 132
    readonly property int groupColumnWidth: 90

    onAboutToShow: {
        selectedRows = []
        pendingRows = []
        attachmentsVM.clearError()
    }
    onClosed: pendingRows = []

    Connections {
        target: attachmentsVM
        function onModelAboutToBeReset() {
            dialog.selectedRows = []
            dialog.pendingRows = []
        }
    }

    function toggleRow(row) {
        const rows = selectedRows.slice()
        const offset = rows.indexOf(row)
        if (offset >= 0)
            rows.splice(offset, 1)
        else
            rows.push(row)
        selectedRows = rows
    }

    function chooseExtractionDestination() {
        if (selectedRows.length === 1) {
            const suggested = attachmentsVM.extractionFileUrl(selectedRows[0])
            if (attachmentsVM.lastError.length)
                return
            pendingRows = selectedRows.slice()
            savePicker.selectedFile = suggested
            savePicker.open()
        } else if (selectedRows.length > 1 && attachmentsVM.validateExtraction(selectedRows)) {
            pendingRows = selectedRows.slice()
            folderPicker.open()
        }
    }

    contentItem: ColumnLayout {
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 48
            Layout.rightMargin: 14
            spacing: 8
            Label { text: i18n.t("Attachment name"); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: i18n.t("Group"); font.bold: true; Layout.preferredWidth: dialog.groupColumnWidth; elide: Text.ElideRight }
            Label { text: i18n.t("Encoded size"); font.bold: true; Layout.preferredWidth: dialog.sizeColumnWidth; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                visible: attachmentsVM.count === 0
                text: i18n.t("No attachments")
            }
            ListView {
                id: list
                objectName: "attachmentsList"
                anchors.fill: parent
                clip: true
                model: attachmentsVM
                Accessible.name: i18n.t("Attachments")
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckDelegate {
                    id: entry
                    required property int index
                    required property string name
                    required property string group
                    required property real size
                    objectName: "attachmentsSelect_" + index
                    width: list.width - 10
                    leftPadding: 48
                    rightPadding: 4
                    height: 44
                    indicator.x: 8
                    checked: dialog.selectedRows.indexOf(index) >= 0
                    Accessible.name: name
                    // Selection belongs to the dialog, not to recycled delegates.
                    nextCheckState: function() { return checked ? Qt.Checked : Qt.Unchecked }
                    onClicked: dialog.toggleRow(index)
                    contentItem: RowLayout {
                        spacing: 8
                        Label { text: entry.name; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: entry.group; Layout.preferredWidth: dialog.groupColumnWidth; elide: Text.ElideRight }
                        Label {
                            text: Number(entry.size).toLocaleString(Qt.locale(), "f", 0) + " " + i18n.t("bytes")
                            Layout.preferredWidth: dialog.sizeColumnWidth
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: entry.name
                }
            }
        }

        Label {
            objectName: "attachmentsError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: attachmentsVM.lastError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: themeVM.danger
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            ToolDialogButton {
                objectName: "attachmentsAttachFontsButton"
                text: i18n.t("Attach Fonts")
                onClicked: { attachmentsVM.clearError(); fontPicker.open() }
            }
            ToolDialogButton {
                objectName: "attachmentsAttachGraphicsButton"
                text: i18n.t("Attach Graphics")
                onClicked: { attachmentsVM.clearError(); graphicsPicker.open() }
            }
            ToolDialogButton {
                objectName: "attachmentsExtractButton"
                text: i18n.t("Extract")
                enabled: dialog.selectedRows.length > 0
                onClicked: dialog.chooseExtractionDestination()
            }
            ToolDialogButton {
                objectName: "attachmentsDeleteButton"
                text: i18n.t("Delete")
                enabled: dialog.selectedRows.length > 0
                onClicked: attachmentsVM.deleteAttachments(dialog.selectedRows)
            }
            ToolDialogButton {
                objectName: "attachmentsCloseButton"
                text: i18n.t("Close")
                onClicked: dialog.close()
            }
        }
    }

    Dialogs.FileDialog {
        id: fontPicker
        objectName: "attachmentsFontPicker"
        title: i18n.t("Choose Fonts to Attach")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [i18n.t("Font Files") + " (*.ttf *.ttc *.pfb)"]
        onAccepted: attachmentsVM.attachFonts(selectedFiles)
    }

    Dialogs.FileDialog {
        id: graphicsPicker
        objectName: "attachmentsGraphicsPicker"
        title: i18n.t("Choose Graphics to Attach")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [i18n.t("Graphic Files") + " (*.bmp *.gif *.jpg *.ico *.wmf)"]
        onAccepted: attachmentsVM.attachGraphics(selectedFiles)
    }

    Dialogs.FileDialog {
        id: savePicker
        objectName: "attachmentsSavePicker"
        title: i18n.t("Extract Attachment")
        fileMode: Dialogs.FileDialog.SaveFile
        onAccepted: {
            const rows = dialog.pendingRows
            dialog.pendingRows = []
            if (rows.length === 1)
                attachmentsVM.extractAttachment(rows[0], selectedFile)
        }
        onRejected: dialog.pendingRows = []
    }

    Dialogs.FolderDialog {
        id: folderPicker
        objectName: "attachmentsFolderPicker"
        title: i18n.t("Extract Attachments to Folder")
        onAccepted: {
            const rows = dialog.pendingRows
            dialog.pendingRows = []
            if (rows.length > 1)
                attachmentsVM.extractAttachments(rows, selectedFolder)
        }
        onRejected: dialog.pendingRows = []
    }
}
