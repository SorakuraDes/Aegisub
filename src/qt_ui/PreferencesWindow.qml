import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: preferencesWindow
    width: 560
    height: 420
    minimumWidth: 440
    minimumHeight: 360
    title: i18n.t("Preferences")
    color: "#1e1e1e"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: i18n.t("Backup")
            font.pixelSize: 22
            font.bold: true
            color: "white"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#3a3a3a"
        }

        Label {
            Layout.fillWidth: true
            text: i18n.t("Automatic Save")
            font.pixelSize: 15
            font.bold: true
            color: "#dddddd"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: i18n.t("Enable")
                font.pixelSize: 14
                color: "#cccccc"
            }

            Item { Layout.fillWidth: true }

            Switch {
                id: autoSaveSwitch
                checked: preferencesVM.autoSave
                onToggled: preferencesVM.autoSave = checked
            }
        }

        Label {
            Layout.fillWidth: true
            text: "config.json: " + preferencesVM.configFilePath
            font.pixelSize: 11
            color: "#888888"
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Close"
            onClicked: preferencesWindow.close()
        }
    }
}
