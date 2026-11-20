import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: propertiesWindow
    width: 560
    height: 480
    minimumWidth: 440
    minimumHeight: 380
    title: i18n.t("Properties")
    color: "#1e1e1e"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: i18n.t("Properties")
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
            text: i18n.t("Title")
            font.pixelSize: 13
            color: "#cccccc"
        }

        TextField {
            id: titleField
            Layout.fillWidth: true
            placeholderText: i18n.t("Title")
            text: propertiesVM.scriptTitle
            onTextEdited: propertiesVM.scriptTitle = text
        }

        Label {
            text: i18n.t("Resolution")
            font.pixelSize: 13
            color: "#cccccc"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            SpinBox {
                id: resX
                Layout.fillWidth: true
                from: 0
                to: 16384
                editable: true
                value: propertiesVM.playResX
                onValueChanged: if (propertiesVM.playResX !== value) propertiesVM.playResX = value
            }

            Label {
                text: "x"
                font.pixelSize: 14
                color: "#aaaaaa"
            }

            SpinBox {
                id: resY
                Layout.fillWidth: true
                from: 0
                to: 16384
                editable: true
                value: propertiesVM.playResY
                onValueChanged: if (propertiesVM.playResY !== value) propertiesVM.playResY = value
            }
        }

        Label {
            Layout.fillWidth: true
            text: i18n.t("PlayResX") + " = " + propertiesVM.playResX
                  + ", " + i18n.t("PlayResY") + " = " + propertiesVM.playResY
            font.pixelSize: 11
            color: "#888888"
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: i18n.t("Print ASS to Console")
                onClicked: propertiesVM.dumpAss()
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Close"
                onClicked: propertiesWindow.close()
            }
        }
    }
}
