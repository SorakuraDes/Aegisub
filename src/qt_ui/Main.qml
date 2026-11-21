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

    // Pending File-menu action while the unsaved-changes dialog is up, and the
    // action to resume once a (possibly Save As) save finished.
    property string pendingAction: ""
    property string pendingAfterSave: ""
    // Set once the user chose to discard; lets close() get through onClosing.
    property bool forceClose: false

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

    // Run a File-menu action. "quit" is routed through close() so the window
    // close guard (onClosing) is the only place that decides about quitting.
    function performAction(action) {
        if (action === "new") {
            documentVM.newDocument()
        } else if (action === "open") {
            openDialog.open()
        } else if (action === "quit") {
            forceClose = true
            mainWindow.close()
        }
        pendingAction = ""
    }

    // Used by File -> New / Open and the window close box: never throw away
    // unsaved changes without asking.
    function requestAction(action) {
        if (documentVM.dirty) {
            pendingAction = action
            unsavedDialog.open()
            return
        }
        performAction(action)
    }

    function saveThenContinue() {
        if (documentVM.filePath === "") {
            // No file name yet: ask for one, then resume the pending action.
            pendingAfterSave = pendingAction
            saveAsDialog.open()
            return
        }
        if (documentVM.saveFile())
            performAction(pendingAction)
    }

    onClosing: function(close) {
        if (!forceClose && documentVM.dirty) {
            close.accepted = false
            requestAction("quit")
        }
    }

    menuBar: MenuBar {
        Menu {
            title: "&File"
            Action {
                text: "&New"
                shortcut: StandardKey.New
                onTriggered: mainWindow.requestAction("new")
            }
            Action {
                text: "&Open..."
                shortcut: StandardKey.Open
                onTriggered: mainWindow.requestAction("open")
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
                onTriggered: mainWindow.requestAction("quit")
            }
        }

        Menu {
            title: "&Edit"
            Action {
                // Dynamic label in the style of the wxWidgets frontend:
                // "Undo <what will be undone>".
                text: undoVM.canUndo && undoVM.undoDescription.length > 0
                      ? "&Undo " + undoVM.undoDescription
                      : "&Undo"
                shortcut: StandardKey.Undo
                enabled: undoVM.canUndo
                onTriggered: undoVM.undo()
            }
            Action {
                text: undoVM.canRedo && undoVM.redoDescription.length > 0
                      ? "&Redo " + undoVM.redoDescription
                      : "&Redo"
                shortcut: StandardKey.Redo
                enabled: undoVM.canRedo
                onTriggered: undoVM.redo()
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
        onAccepted: {
            if (documentVM.saveFileAs(selectedFile) && mainWindow.pendingAfterSave !== "") {
                var action = mainWindow.pendingAfterSave
                mainWindow.pendingAfterSave = ""
                mainWindow.performAction(action)
            }
        }
    }

    // Material 3 style guard against losing unsaved changes.
    Dialog {
        id: unsavedDialog
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        title: i18n.t("Unsaved Changes")
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                text: i18n.t("The current subtitle file has unsaved changes.")
                      + "\n" + i18n.t("Do you want to save them before continuing?")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: i18n.t("Save")
                    highlighted: true
                    onClicked: {
                        unsavedDialog.close()
                        mainWindow.saveThenContinue()
                    }
                }
                Button {
                    text: i18n.t("Discard")
                    onClicked: {
                        unsavedDialog.close()
                        mainWindow.performAction(mainWindow.pendingAction)
                    }
                }
                Button {
                    text: i18n.t("Cancel")
                    onClicked: {
                        unsavedDialog.close()
                        mainWindow.pendingAction = ""
                        mainWindow.pendingAfterSave = ""
                    }
                }
            }
        }
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
