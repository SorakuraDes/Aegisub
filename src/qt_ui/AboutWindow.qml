import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: aboutWindow
    width: 500
    height: 450
    minimumWidth: 400
    minimumHeight: 360
    title: i18n.t("About Aegisub")
    color: themeVM.surface

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        onActivated: aboutWindow.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: aboutVM.appName
            font.pixelSize: 24
            font.bold: true
            color: themeVM.onSurface
        }

        Label {
            Layout.fillWidth: true
            text: aboutVM.versionInfo
            font.pixelSize: 14
            color: themeVM.onSurfaceVariant
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: aboutVM.copyright
            font.pixelSize: 13
            color: themeVM.onSurfaceVariant
            wrapMode: Text.Wrap
        }

        // The panel gets its own Rectangle so the TextArea never has to be
        // stretched to the viewport. TextArea height follows its content
        // (implicitHeight), which keeps the ScrollView scrolling correct and
        // avoids the top line being clipped.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themeVM.surfaceContainer
            radius: themeVM.radiusMedium

            ScrollView {
                id: translatorsScroll
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    width: translatorsScroll.availableWidth
                    height: implicitHeight
                    text: aboutVM.translatorsText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: themeVM.onSurface
                    padding: 0
                    background: null
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: i18n.t("Close")
            onClicked: aboutWindow.close()
        }
    }
}
