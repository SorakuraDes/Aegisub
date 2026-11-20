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
    property var propertiesWindow: null

    function windowFor(kind) {
        if (kind === "about")
            return aboutWindow
        if (kind === "preferences")
            return preferencesWindow
        return propertiesWindow
    }

    function setWindow(kind, win) {
        if (kind === "about")
            aboutWindow = win
        else if (kind === "preferences")
            preferencesWindow = win
        else
            propertiesWindow = win
    }

    function openWindow(kind) {
        var existing = windowFor(kind)
        if (existing !== null) {
            existing.show()
            existing.raise()
            existing.requestActivate()
            return
        }

        var url = "qrc:/" + (kind === "about" ? "AboutWindow.qml"
                            : kind === "preferences" ? "PreferencesWindow.qml"
                            : "PropertiesWindow.qml")
        var component = Qt.createComponent(url)
        if (component.status !== Component.Ready) {
            console.error("Failed to load", url, component.errorString())
            return
        }
        var win = component.createObject(null)
        setWindow(kind, win)
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

        Button {
            text: "Show Properties"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: mainWindow.openWindow("properties")
        }
    }
}
