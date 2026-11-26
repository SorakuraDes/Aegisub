import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: root
    objectName: "fontsCollectorWindow"
    title: i18n.t("Fonts Collector")
    width: 650
    height: 530
    minimumWidth: 500
    minimumHeight: 460
    color: themeVM.surface
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.foreground: themeVM.onSurface
    Material.background: themeVM.surfaceContainer

    readonly property bool editableDestination: fontsCollectorVM.action === 1 || fontsCollectorVM.action === 3
    function open() { show() }
    onClosing: function(event) { event.accepted = !fontsCollectorVM.running }
    Shortcut { sequence: "Escape"; enabled: root.visible && !fontsCollectorVM.running; onActivated: root.close() }

    Rectangle {
        objectName: "fontsCollectorBackground"
        anchors.fill: parent
        color: themeVM.surface
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 7

        Label { text: i18n.t("Action"); font.bold: true; color: themeVM.onSurface }
        ButtonGroup { id: modes }
        Repeater {
            model: [i18n.t("Check fonts for availability"), i18n.t("Copy fonts to folder"),
                    i18n.t("Copy fonts to subtitle file's folder"), i18n.t("Copy fonts to zipped archive")]
            RadioButton {
                required property int index
                required property string modelData
                objectName: "fontsCollectorAction" + index
                text: modelData
                enabled: !fontsCollectorVM.running && (index !== 2 || fontsCollectorVM.hasScriptPath)
                checked: fontsCollectorVM.action === index
                ButtonGroup.group: modes
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                topPadding: 0
                bottomPadding: 0
                onClicked: fontsCollectorVM.action = index
            }
        }

        Label { text: i18n.t("Destination"); font.bold: true; color: themeVM.onSurface }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: destinationField
                objectName: "fontsCollectorDestination"
                Layout.fillWidth: true
                selectByMouse: true
                enabled: root.editableDestination && !fontsCollectorVM.running
                Component.onCompleted: text = fontsCollectorVM.destination
                onTextEdited: fontsCollectorVM.destination = text
                Connections {
                    target: fontsCollectorVM
                    function onDestinationChanged() {
                        if (destinationField.text !== fontsCollectorVM.destination)
                            destinationField.text = fontsCollectorVM.destination
                    }
                }
            }
            ToolDialogButton {
                objectName: "fontsCollectorBrowseButton"
                enabled: destinationField.enabled
                text: i18n.t("&Browse...")
                onClicked: {
                    const picker = fontsCollectorVM.action === 3 ? archivePicker : folderPicker
                    picker.currentFolder = fontsCollectorVM.browseFolder()
                    picker.open()
                }
            }
        }

        Label { text: i18n.t("Log"); font.bold: true; color: themeVM.onSurface }
        ScrollView {
            id: logScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextArea {
                objectName: "fontsCollectorLog"
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                font.family: "Consolas"
                color: themeVM.onSurface
                text: fontsCollectorVM.logText
                background: Rectangle { color: themeVM.surfaceContainerLow; border.color: themeVM.outlineVariant }
                onTextChanged: Qt.callLater(function() {
                    const flick = logScroll.contentItem
                    flick.contentY = Math.max(0, flick.contentHeight - flick.height)
                })
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: fontsCollectorVM.outputFailed ? themeVM.danger : themeVM.onSurfaceVariant
                text: fontsCollectorVM.running || fontsCollectorVM.outputFailed || fontsCollectorVM.statusText.length === 0
                      ? i18n.t(fontsCollectorVM.statusText)
                      : i18n.t("Font files found: %1; missing: %2").arg(fontsCollectorVM.collectedCount).arg(fontsCollectorVM.missingCount)
            }
            ToolDialogButton {
                objectName: "fontsCollectorStartButton"
                primary: true
                text: i18n.t("&Start!")
                enabled: !fontsCollectorVM.running && (fontsCollectorVM.action !== 2 || fontsCollectorVM.hasScriptPath)
                onClicked: fontsCollectorVM.start()
            }
            ToolDialogButton {
                objectName: "fontsCollectorCopyLogButton"
                enabled: fontsCollectorVM.logText.length > 0
                text: i18n.t("Copy Log")
                onClicked: fontsCollectorVM.copyLog()
            }
            ToolDialogButton {
                objectName: "fontsCollectorCloseButton"
                text: i18n.t("Close")
                enabled: !fontsCollectorVM.running
                onClicked: root.close()
            }
        }
    }

    FolderDialog {
        id: folderPicker
        title: i18n.t("Select folder to save fonts on")
        onAccepted: fontsCollectorVM.selectDestination(selectedFolder)
    }
    FileDialog {
        id: archivePicker
        title: i18n.t("Select archive file name")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [i18n.t("Zip Archives (*.zip)")]
        onAccepted: fontsCollectorVM.selectDestination(selectedFile)
    }
}
