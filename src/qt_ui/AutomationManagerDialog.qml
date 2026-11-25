import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Dialog {
    id: manager
    objectName: "automationManagerDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(700, parent ? parent.width - 32 : 700)
    height: 375
    title: i18n.t("Automation Manager")
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    readonly property var scripts: automationVM.scripts
    readonly property var selectedScript: scriptList.currentIndex >= 0
                                          && scriptList.currentIndex < scripts.length
                                          ? scripts[scriptList.currentIndex] : null

    onOpened: scriptList.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item { Layout.preferredWidth: 28 }
            Label {
                Layout.preferredWidth: scriptList.width * 0.25
                text: i18n.t("Name")
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.preferredWidth: scriptList.width * 0.23
                text: i18n.t("Filename")
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: i18n.t("Description")
                font.bold: true
                elide: Text.ElideRight
            }
        }

        ListView {
            id: scriptList
            objectName: "automationScriptList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            currentIndex: -1
            keyNavigationEnabled: true
            Accessible.name: manager.title
            model: manager.scripts
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: scriptRow
                required property int index
                required property var modelData
                readonly property bool hasError: !!modelData.error
                objectName: "automationScriptRow_" + index
                width: ListView.view.width
                implicitHeight: 32
                leftPadding: 0
                rightPadding: 12
                highlighted: ListView.isCurrentItem
                Accessible.name: modelData.name || modelData.fileName || ""
                Accessible.description: modelData.error || modelData.description || ""
                ToolTip {
                    visible: scriptRow.hovered && text.length > 0
                    delay: 600
                    text: scriptRow.modelData.path || ""
                }
                onClicked: {
                    scriptList.currentIndex = index
                    scriptList.forceActiveFocus()
                }
                contentItem: RowLayout {
                    spacing: 10

                    Label {
                        Layout.preferredWidth: 28
                        horizontalAlignment: Text.AlignHCenter
                        text: scriptRow.modelData.autoload ? "G" : "L"
                        color: scriptRow.hasError ? themeVM.danger : themeVM.onSurfaceVariant
                        font.bold: scriptRow.hasError
                    }
                    Label {
                        Layout.preferredWidth: scriptList.width * 0.25
                        text: scriptRow.modelData.name || scriptRow.modelData.fileName || ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: scriptList.width * 0.23
                        text: scriptRow.modelData.fileName || ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: scriptRow.modelData.error || scriptRow.modelData.description || ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: scriptRow.hasError ? themeVM.danger : themeVM.onSurface
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: scriptList.count === 0
                text: i18n.t("Empty")
                color: themeVM.onSurfaceVariant
            }
        }
    }

    footer: Flow {
        leftPadding: 12
        rightPadding: 12
        bottomPadding: 8
        spacing: 6
        Button {
            objectName: "automationScriptAddButton"
            text: i18n.t("Add")
            onClicked: {
                const folder = automationVM.lastFolder
                if (folder && String(folder).length > 0)
                    scriptPicker.currentFolder = folder
                scriptPicker.open()
            }
        }
        Button {
            objectName: "automationScriptRemoveButton"
            text: i18n.t("Remove")
            enabled: manager.selectedScript !== null && !manager.selectedScript.autoload
            onClicked: automationVM.removeScript(scriptList.currentIndex)
        }
        Button {
            objectName: "automationScriptReloadButton"
            text: i18n.t("Reload")
            enabled: manager.selectedScript !== null
            onClicked: automationVM.reloadScript(scriptList.currentIndex)
        }
        Button {
            objectName: "automationScriptInfoButton"
            text: i18n.t("Show Info")
            onClicked: {
                infoText.text = automationVM.scriptInfo(scriptList.currentIndex)
                infoDialog.open()
            }
        }
        Button {
            objectName: "automationScriptRescanButton"
            text: i18n.t("Rescan Autoload Dir")
            onClicked: automationVM.reloadAutoload()
        }
        Button {
            objectName: "automationScriptCloseButton"
            text: i18n.t("Close")
            onClicked: manager.close()
        }
    }

    Dialogs.FileDialog {
        id: scriptPicker
        objectName: "automationScriptFileDialog"
        title: i18n.t("Add Automation script")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [i18n.t("Lua scripts") + " (*.lua *.moon)", i18n.t("All Files") + " (*)"]
        onAccepted: automationVM.addScripts(selectedFiles)
    }

    Dialog {
        id: infoDialog
        objectName: "automationScriptInfoDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(600, parent ? parent.width - 32 : 600)
        height: 390
        title: i18n.t("Automation Script Info")
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        contentItem: ScrollView {
            contentWidth: availableWidth
            TextArea {
                id: infoText
                objectName: "automationScriptInfoText"
                readOnly: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
            }
        }
    }
}
