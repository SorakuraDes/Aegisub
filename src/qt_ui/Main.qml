import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1000
    height: 640
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Top bar: the former test buttons, kept out of the grid's way.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: "About"
                onClicked: mainWindow.openWindow("about")
            }
            Button {
                text: "Preferences"
                onClicked: mainWindow.openWindow("preferences")
            }
            Button {
                text: "Properties"
                onClicked: mainWindow.openWindow("properties")
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "Aegisub Core Version: " + appInfo.version
                font.pixelSize: 12
                opacity: 0.8
            }
        }

        // The subtitle grid takes over the rest of the window.
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: "qrc:/SubtitleGrid.qml"
        }
    }
}
