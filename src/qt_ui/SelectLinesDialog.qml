import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: root
    objectName: "selectLinesDialog"
    title: i18n.t("Select Lines")
    modality: Qt.NonModal
    readonly property int preferredWidth: Math.max(480, form.implicitWidth + 2 * form.anchors.margins)
    readonly property int preferredHeight: form.implicitHeight + 2 * form.anchors.margins
    width: preferredWidth
    height: preferredHeight
    minimumWidth: nativeWindowRaise ? preferredWidth : 0
    minimumHeight: nativeWindowRaise ? preferredHeight : 0
    color: themeVM.surface
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    function openForSelectLines() {
        if (!visible)
            selectLinesVM.clearStatus()
        show()
        requestActivate()
        queryField.forceActiveFocus()
        queryField.selectAll()
    }

    function accept() {
        if (selectLinesVM.applySelection()) root.close()
    }

    onClosing: selectLinesVM.saveSettings()

    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: root.close()
    }

    ColumnLayout {
        id: form
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        GroupBox {
            title: i18n.t("Match")
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Layout.fillWidth: true
                    RadioButton { objectName: "matchesRadio"; text: i18n.t("Matches"); checked: selectLinesVM.condition === 0; onClicked: selectLinesVM.condition = 0 }
                    RadioButton { objectName: "doesNotMatchRadio"; text: i18n.t("Doesn't Match"); checked: selectLinesVM.condition === 1; onClicked: selectLinesVM.condition = 1 }
                    CheckBox { objectName: "caseCheckBox"; text: i18n.t("Match case"); checked: selectLinesVM.caseSensitive; onToggled: selectLinesVM.caseSensitive = checked }
                }
                TextField {
                    id: queryField
                    objectName: "selectLinesText"
                    Layout.fillWidth: true
                    implicitHeight: themeVM.controlHeight
                    Accessible.name: i18n.t("Text")
                    text: selectLinesVM.query
                    selectByMouse: true
                    onTextEdited: selectLinesVM.query = text
                    onAccepted: root.accept()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: i18n.t("Mode"); color: themeVM.onSurfaceVariant }
            ComboBox {
                objectName: "matchModeCombo"
                Accessible.name: i18n.t("Mode")
                implicitContentWidthPolicy: ComboBox.WidestText
                Layout.fillWidth: true
                model: [i18n.t("Exact match"), i18n.t("Contains"), i18n.t("Regular Expression match")]
                currentIndex: selectLinesVM.matchMode
                onActivated: selectLinesVM.matchMode = currentIndex
            }
            Label { text: i18n.t("In Field"); color: themeVM.onSurfaceVariant }
            ComboBox {
                objectName: "fieldCombo"
                Accessible.name: i18n.t("In Field")
                implicitContentWidthPolicy: ComboBox.WidestText
                Layout.fillWidth: true
                model: [i18n.t("Text"), i18n.t("Style"), i18n.t("Actor"), i18n.t("Effect")]
                currentIndex: selectLinesVM.field
                onActivated: selectLinesVM.field = currentIndex
            }
        }

        GroupBox {
            title: i18n.t("Match dialogues/comments")
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                CheckBox {
                    objectName: "dialogueCheckBox"
                    text: i18n.t("Dialogues")
                    checked: selectLinesVM.includeDialogue
                    enabled: !checked || selectLinesVM.includeComments
                    onToggled: selectLinesVM.includeDialogue = checked
                }
                CheckBox {
                    objectName: "commentCheckBox"
                    text: i18n.t("Comments")
                    checked: selectLinesVM.includeComments
                    enabled: !checked || selectLinesVM.includeDialogue
                    onToggled: selectLinesVM.includeComments = checked
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: i18n.t("Action"); color: themeVM.onSurfaceVariant }
            ComboBox {
                objectName: "selectionActionCombo"
                Accessible.name: i18n.t("Action")
                implicitContentWidthPolicy: ComboBox.WidestText
                Layout.fillWidth: true
                model: [i18n.t("Set selection"), i18n.t("Add to selection"), i18n.t("Subtract from selection"), i18n.t("Intersect with selection")]
                currentIndex: selectLinesVM.selectionAction
                onActivated: selectLinesVM.selectionAction = currentIndex
            }
        }

        Label {
            objectName: "selectLinesStatus"
            Layout.fillWidth: true
            text: !selectLinesVM.statusMessage ? ""
                  : selectLinesVM.statusError ? i18n.t(selectLinesVM.statusMessage)
                  : i18n.t("Matched %1 line(s); selected %2 line(s)").arg(selectLinesVM.matchedCount).arg(selectLinesVM.selectedCount)
            textFormat: Text.PlainText
            color: selectLinesVM.statusError ? themeVM.danger : themeVM.onSurfaceVariant
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            ToolDialogButton { objectName: "okButton"; text: i18n.t("OK"); primary: true; onClicked: root.accept() }
            Button { objectName: "applyButton"; text: i18n.t("Apply"); onClicked: selectLinesVM.applySelection() }
            Item { Layout.fillWidth: true }
            Button { objectName: "cancelButton"; text: i18n.t("Cancel"); onClicked: root.close() }
        }
    }
}
