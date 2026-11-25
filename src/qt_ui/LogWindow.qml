import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: logWindow
    objectName: "logWindow"
    width: 760
    height: 440
    minimumWidth: 420
    minimumHeight: 240
    title: i18n.t("Log Window")
    modality: Qt.NonModal
    color: themeVM.surface

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    onVisibleChanged: logVM.setVisible(visible)

    Shortcut {
        context: Qt.WindowShortcut
        sequence: StandardKey.Cancel
        onActivated: logWindow.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: themeVM.radiusSmall
            color: themeVM.surfaceContainer

            ScrollView {
                id: logScroll
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    objectName: "logText"
                    Accessible.name: i18n.t("Log")
                    width: logScroll.availableWidth
                    height: implicitHeight
                    text: logVM.text
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    font.pixelSize: 13
                    color: themeVM.onSurface
                    padding: 0
                    background: null
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "logCopyButton"
                text: i18n.t("Copy")
                enabled: logVM.text.length > 0
                onClicked: logVM.copy()
            }
            Button {
                objectName: "logClearButton"
                text: i18n.t("Clear")
                enabled: logVM.text.length > 0
                onClicked: logVM.clear()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: i18n.t("Close")
                onClicked: logWindow.close()
            }
        }
    }
}
