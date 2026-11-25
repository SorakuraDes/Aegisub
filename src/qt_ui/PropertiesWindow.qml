import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: propertiesWindow
    width: 440
    height: 310
    minimumWidth: Math.max(380, propertiesForm.implicitWidth + 2 * propertiesForm.anchors.margins)
    minimumHeight: propertiesForm.implicitHeight + 2 * propertiesForm.anchors.margins
    title: i18n.t("Properties")
    color: themeVM.surface

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        onActivated: propertiesWindow.close()
    }

    ColumnLayout {
        id: propertiesForm
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        Label {
            text: i18n.t("Title")
            font.pixelSize: 13
            color: themeVM.onSurfaceVariant
        }

        TextField {
            id: titleField
            Layout.fillWidth: true
            implicitHeight: themeVM.controlHeight
            Accessible.name: i18n.t("Title")
            text: propertiesVM.scriptTitle
            onTextEdited: propertiesVM.scriptTitle = text
        }

        Label {
            text: i18n.t("Resolution")
            font.pixelSize: 13
            color: themeVM.onSurfaceVariant
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            SpinBox {
                id: resX
                Layout.fillWidth: true
                implicitHeight: themeVM.controlHeight
                Accessible.name: i18n.t("PlayResX")
                from: 0
                to: 16384
                editable: true
                value: propertiesVM.playResX
                onValueChanged: if (propertiesVM.playResX !== value) propertiesVM.playResX = value
            }

            Label {
                text: "x"
                font.pixelSize: 14
                color: themeVM.onSurfaceVariant
            }

            SpinBox {
                id: resY
                Layout.fillWidth: true
                implicitHeight: themeVM.controlHeight
                Accessible.name: i18n.t("PlayResY")
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
            color: themeVM.onSurfaceVariant
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
                text: i18n.t("Close")
                onClicked: propertiesWindow.close()
            }
        }
    }
}
