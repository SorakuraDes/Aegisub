import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1000
    height: 700
    title: documentVM.displayName + (documentVM.dirty ? " *" : "") + " - Aegisub Qt6"

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

    function saveDocument() {
        if (documentVM.filePath === "")
            saveAsDialog.open()
        else
            documentVM.saveFile()
    }

    menuBar: MenuBar {
        Menu {
            title: "&File"
            Action {
                text: "&New"
                shortcut: StandardKey.New
                onTriggered: documentVM.newDocument()
            }
            Action {
                text: "&Open..."
                shortcut: StandardKey.Open
                onTriggered: openDialog.open()
            }
            Action {
                text: "&Save"
                shortcut: StandardKey.Save
                onTriggered: mainWindow.saveDocument()
            }
            Action {
                text: "Save &As..."
                shortcut: StandardKey.SaveAs
                onTriggered: saveAsDialog.open()
            }
            MenuSeparator {}
            Action {
                text: "E&xit"
                shortcut: StandardKey.Quit
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: "&Subtitle"
            Action {
                text: "&Properties..."
                onTriggered: mainWindow.openWindow("properties")
            }
        }

        Menu {
            title: "&Tools"
            Action {
                text: "&Preferences..."
                onTriggered: mainWindow.openWindow("preferences")
            }
        }

        Menu {
            title: "&Help"
            Action {
                text: "&About Aegisub"
                onTriggered: mainWindow.openWindow("about")
            }
        }
    }

    FileDialog {
        id: openDialog
        title: "Open subtitle file"
        nameFilters: ["ASS subtitles (*.ass *.ssa)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: documentVM.openFile(selectedFile)
    }

    FileDialog {
        id: saveAsDialog
        title: "Save subtitle file as"
        nameFilters: ["ASS subtitles (*.ass *.ssa)", "All files (*)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ass"
        onAccepted: documentVM.saveFileAs(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Error banner (e.g. failed open/save); closable.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            visible: documentVM.lastError.length > 0
            color: "#5a1f1f"
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: documentVM.lastError
                    color: "#ffd0d0"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Button {
                    text: "\u2715"
                    flat: true
                    font.pixelSize: 12
                    onClicked: documentVM.clearError()
                }
            }
        }

        // The subtitle grid takes over the middle of the window.
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: "qrc:/SubtitleGrid.qml"
        }

        // Single-line editor for the selected row.
        Loader {
            Layout.fillWidth: true
            source: "qrc:/EditBox.qml"
        }
    }
}
