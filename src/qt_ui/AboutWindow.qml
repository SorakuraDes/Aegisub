import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: aboutWindow
    width: 560
    height: 560
    minimumWidth: 420
    minimumHeight: 420
    title: "About Aegisub"
    color: "#1e1e1e"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: aboutVM.appName
            font.pixelSize: 24
            font.bold: true
            color: "white"
        }

        Label {
            Layout.fillWidth: true
            text: aboutVM.versionInfo
            font.pixelSize: 14
            color: "#cccccc"
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: aboutVM.copyright
            font.pixelSize: 13
            color: "#aaaaaa"
            wrapMode: Text.Wrap
        }

        // The panel gets its own Rectangle so the TextArea never has to be
        // stretched to the viewport. TextArea height follows its content
        // (implicitHeight), which keeps the ScrollView scrolling correct and
        // avoids the top line being clipped.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2a2a2a"
            radius: 6

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
                    color: "#dddddd"
                    padding: 0
                    background: null
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: "Close"
            onClicked: aboutWindow.close()
        }
    }
}
