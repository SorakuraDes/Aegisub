import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: preferencesWindow
    width: 480
    height: 345
    minimumWidth: Math.max(420, preferencesForm.implicitWidth + 2 * preferencesForm.anchors.margins)
    minimumHeight: preferencesForm.implicitHeight + 2 * preferencesForm.anchors.margins
    title: i18n.t("Preferences")
    color: themeVM.surface
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        onActivated: preferencesWindow.close()
    }

    ColumnLayout {
        id: preferencesForm
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        Label {
            text: i18n.t("Appearance")
            color: themeVM.onSurface
            font.pixelSize: 18
            font.bold: true
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: i18n.t("Dark mode"); color: themeVM.onSurface }
            Item { Layout.fillWidth: true }
            Switch {
                Accessible.name: i18n.t("Dark mode")
                checked: themeVM.darkMode
                onToggled: themeVM.darkMode = checked
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Label { text: i18n.t("Accent color"); color: themeVM.onSurface }
            Item { Layout.fillWidth: true }
            Repeater {
                model: themeVM.presetColors
                delegate: ToolButton {
                    id: swatch
                    required property var modelData
                    readonly property bool selected: Qt.colorEqual(themeVM.seedColor, modelData.color)
                    Layout.preferredWidth: themeVM.controlHeight
                    Layout.preferredHeight: themeVM.controlHeight
                    padding: 0
                    hoverEnabled: true
                    Accessible.name: i18n.t(modelData.name)
                    Accessible.checkable: true
                    Accessible.checked: selected
                    ToolTip {
                        visible: swatch.hovered || swatch.visualFocus
                        text: i18n.t(swatch.modelData.name)
                        delay: 400
                    }
                    onClicked: themeVM.seedColor = modelData.color
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: themeVM.surface
                            border.width: swatch.visualFocus ? 3 : swatch.selected ? 2 : 1
                            border.color: swatch.visualFocus || swatch.selected ? themeVM.onSurface : themeVM.outlineVariant
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: width / 2
                                color: swatch.modelData.color
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: i18n.t("Custom color"); color: themeVM.onSurface }
            Item { Layout.fillWidth: true }
            TextField {
                id: hexField
                Layout.preferredWidth: 120
                implicitHeight: themeVM.controlHeight
                Accessible.name: i18n.t("Custom color")
                text: themeVM.seedColor.toString().toUpperCase()
                selectByMouse: true
                maximumLength: 7
                validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                color: acceptableInput ? themeVM.onSurface : themeVM.danger
                onTextChanged: if (/^#[0-9a-fA-F]{6}$/.test(text) && !Qt.colorEqual(themeVM.seedColor, text))
                                   themeVM.seedColor = text
                onActiveFocusChanged: if (!activeFocus && !acceptableInput) undoInvalidEdit()
                function undoInvalidEdit() {
                    text = Qt.binding(function() { return themeVM.seedColor.toString().toUpperCase() })
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: themeVM.outlineVariant
        }
        Label {
            text: i18n.t("Backup")
            color: themeVM.onSurface
            font.pixelSize: 18
            font.bold: true
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: i18n.t("Automatic Save"); color: themeVM.onSurface }
            Item { Layout.fillWidth: true }
            Switch {
                Accessible.name: i18n.t("Automatic Save")
                checked: preferencesVM.autoSave
                onToggled: preferencesVM.autoSave = checked
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            text: "config.json: " + preferencesVM.configFilePath
            font.pixelSize: 11
            color: themeVM.onSurfaceVariant
            wrapMode: Text.WrapAnywhere
        }
        Item { Layout.fillHeight: true }
        Button {
            Layout.alignment: Qt.AlignRight
            text: i18n.t("Close")
            onClicked: preferencesWindow.close()
        }
    }
}
