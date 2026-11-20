import QtQuick
import QtQuick.Controls.Material

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 640
    height: 480
    title: "Aegisub Qt6 Test"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    property var aboutWindow: null
    property var preferencesWindow: null

    function openWindow(kind) {
        var isAbout = kind === "about"
        var existing = isAbout ? aboutWindow : preferencesWindow
        if (existing !== null) {
            existing.show()
            existing.raise()
            existing.requestActivate()
            return
        }

        var url = isAbout ? "qrc:/AboutWindow.qml" : "qrc:/PreferencesWindow.qml"
        var component = Qt.createComponent(url)
        if (component.status !== Component.Ready) {
            console.error("Failed to load", url, component.errorString())
            return
        }
        var win = component.createObject(null)
        if (isAbout)
            aboutWindow = win
        else
            preferencesWindow = win
        win.show()
        win.raise()
        win.requestActivate()
    }

    Column {
        anchors.centerIn: parent
        spacing: 20

        Button {
            text: "Hello Material 3"
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Label {
            text: "Aegisub Core Version: " + appInfo.version
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: 14
            opacity: 0.85
        }

        Button {
            text: "Show About Dialog"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: mainWindow.openWindow("about")
        }

        Button {
            text: "Show Preferences"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: mainWindow.openWindow("preferences")
        }
    }
}
