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
    property var searchWindow: null
    property var stylesWindow: null

    // Pending File-menu action while the unsaved-changes dialog is up, and the
    // action to resume once a (possibly Save As) save finished.
    property string pendingAction: ""
    property string pendingAfterSave: ""
    // Set once the user chose to discard; lets close() get through onClosing.
    property bool forceClose: false

    // ---- Windows ---------------------------------------------------------

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

    // Non-modal Find & Replace window; both menu entries reuse the same
    // instance and just move the focus to the matching input.
    function openSearch(useReplace) {
        if (searchWindow === null) {
            var component = Qt.createComponent("qrc:/SearchDialog.qml")
            if (component.status !== Component.Ready) {
                console.error("Failed to load SearchDialog.qml", component.errorString())
                return
            }
            searchWindow = component.createObject(null)
        }
        searchWindow.show()
        searchWindow.raise()
        searchWindow.requestActivate()
        searchWindow.focusField(useReplace)
    }

    function openStyles() {
        if (stylesWindow === null) {
            var component = Qt.createComponent("qrc:/StylesWindow.qml")
            if (component.status !== Component.Ready) {
                console.error("Failed to load StylesWindow.qml", component.errorString())
                return
            }
            stylesWindow = component.createObject(null)
        }
        stylesWindow.show()
        stylesWindow.raise()
        stylesWindow.requestActivate()
    }

    function closeChildWindows() {
        if (searchWindow !== null) {
            searchWindow.close()
            searchWindow = null
        }
        if (stylesWindow !== null) {
            stylesWindow.close()
            stylesWindow = null
        }
        if (aboutWindow !== null) {
            aboutWindow.close()
            aboutWindow = null
        }
        if (preferencesWindow !== null) {
            preferencesWindow.close()
            preferencesWindow = null
        }
        if (propertiesWindow !== null) {
            propertiesWindow.close()
            propertiesWindow = null
        }
    }

    // ---- File actions ----------------------------------------------------

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

    // Keyboard focus inside the EditBox fields (or one of their internal
    // editors, e.g. the SpinBox's TextInput): Delete/Insert/Ctrl+D belong to
    // the text editors there, not to the row actions.
    function textInputFocused() {
        var item = mainWindow.activeFocusItem
        while (item !== null && item !== undefined) {
            if (item.isEditBoxField === true)
                return true
            item = item.parent
        }
        return false
    }

    onClosing: function(close) {
        if (!forceClose && documentVM.dirty) {
            close.accepted = false
            requestAction("quit")
            return
        }
        // Do not leave the auxiliary windows (search, styles, ...) open: they
        // would keep the application alive after the main window closes.
        closeChildWindows()
    }

    // Global shortcuts. The menu items are plain MenuItems (they need a custom
    // contentItem to show shortcut hints), so the key bindings live here.
    // Ctrl+Z / Ctrl+Y are handled by main.cpp's key filter.
    Shortcut { sequence: StandardKey.New; onActivated: mainWindow.requestAction("new") }
    Shortcut { sequence: StandardKey.Open; onActivated: mainWindow.requestAction("open") }
    Shortcut { sequence: StandardKey.Save; onActivated: mainWindow.saveDocument() }
    Shortcut { sequence: StandardKey.SaveAs; onActivated: saveAsDialog.open() }
    Shortcut { sequence: StandardKey.Quit; onActivated: mainWindow.requestAction("quit") }
    Shortcut { sequence: StandardKey.Find; onActivated: mainWindow.openSearch(false) }
    Shortcut { sequence: StandardKey.Replace; onActivated: mainWindow.openSearch(true) }

    // Row actions, kept away from the text editors above.
    Shortcut {
        sequence: "Delete"
        enabled: !mainWindow.textInputFocused() && gridModel.selectionCount > 0
        onActivated: gridModel.deleteSelectedRows()
    }
    Shortcut {
        sequence: "Insert"
        enabled: !mainWindow.textInputFocused()
        onActivated: gridModel.insertAfter(gridModel.activeRow)
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: !mainWindow.textInputFocused() && gridModel.selectionCount > 0
        onActivated: gridModel.duplicateSelectedRows()
    }
    Shortcut {
        sequence: StandardKey.SelectAll
        enabled: !mainWindow.textInputFocused()
        onActivated: gridModel.selectAll()
    }

    // Video preview: jump to the active line. Play/step are buttons in the
    // VideoPanel transport bar (no global Space binding on purpose: it would
    // fight every focused button in the window).
    Shortcut {
        sequence: "Ctrl+J"
        enabled: videoVM.hasVideo
        onActivated: videoVM.seekToActiveLine()
    }

    // Audio waveform: play the active line's range.
    Shortcut {
        sequence: "Ctrl+Space"
        enabled: audioVM.hasAudio
        onActivated: audioVM.playSelection()
    }

    // ---- Compact desktop menu bar ----------------------------------------

    menuBar: MenuBar {
        id: mainMenuBar
        padding: 0
        spacing: 0

        background: Rectangle {
            implicitHeight: 28
            color: "#252525"
        }

        delegate: MenuBarItem {
            id: barItem
            implicitHeight: 28
            topPadding: 0
            bottomPadding: 0
            leftPadding: 10
            rightPadding: 10

            contentItem: Label {
                // Qt Quick Labels do not interpret '&' mnemonics, so drop them
                // instead of showing "&File".
                text: (barItem.text.length > 0
                       ? barItem.text
                       : (barItem.action ? barItem.action.text : "")).replace(/&/g, "")
                font.pixelSize: 13
                color: barItem.highlighted ? Material.accent : "#e6e6e6"
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: barItem.highlighted ? "#333333" : "transparent"
            }
        }

        Menu {
            title: "&File"
            ShortcutMenuItem {
                text: i18n.t("New")
                shortcutText: "Ctrl+N"
                onTriggered: mainWindow.requestAction("new")
            }
            ShortcutMenuItem {
                text: i18n.t("Open...")
                shortcutText: "Ctrl+O"
                onTriggered: mainWindow.requestAction("open")
            }
            ShortcutMenuItem {
                text: i18n.t("Save")
                shortcutText: "Ctrl+S"
                onTriggered: mainWindow.saveDocument()
            }
            ShortcutMenuItem {
                text: i18n.t("Save As...")
                shortcutText: "Ctrl+Shift+S"
                onTriggered: saveAsDialog.open()
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Exit")
                shortcutText: "Ctrl+Q"
                onTriggered: mainWindow.requestAction("quit")
            }
        }

        Menu {
            title: "&Edit"
            ShortcutMenuItem {
                // Dynamic label in the style of the wxWidgets frontend:
                // "Undo <what will be undone>".
                text: undoVM.canUndo && undoVM.undoDescription.length > 0
                      ? i18n.t("Undo") + " " + undoVM.undoDescription
                      : i18n.t("Undo")
                shortcutText: "Ctrl+Z"
                enabled: undoVM.canUndo
                onTriggered: undoVM.undo()
            }
            ShortcutMenuItem {
                text: undoVM.canRedo && undoVM.redoDescription.length > 0
                      ? i18n.t("Redo") + " " + undoVM.redoDescription
                      : i18n.t("Redo")
                shortcutText: "Ctrl+Y"
                enabled: undoVM.canRedo
                onTriggered: undoVM.redo()
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Find...")
                shortcutText: "Ctrl+F"
                onTriggered: mainWindow.openSearch(false)
            }
            ShortcutMenuItem {
                text: i18n.t("Replace...")
                shortcutText: "Ctrl+H"
                onTriggered: mainWindow.openSearch(true)
            }
        }

        Menu {
            title: "&Subtitle"
            ShortcutMenuItem {
                text: i18n.t("Insert Before")
                onTriggered: gridModel.insertBefore(gridModel.activeRow)
            }
            ShortcutMenuItem {
                text: i18n.t("Insert After")
                shortcutText: "Insert"
                onTriggered: gridModel.insertAfter(gridModel.activeRow)
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Duplicate")
                shortcutText: "Ctrl+D"
                enabled: gridModel.selectionCount > 0
                onTriggered: gridModel.duplicateSelectedRows()
            }
            ShortcutMenuItem {
                text: i18n.t("Delete")
                shortcutText: "Del"
                enabled: gridModel.selectionCount > 0
                onTriggered: gridModel.deleteSelectedRows()
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Styles Manager...")
                onTriggered: mainWindow.openStyles()
            }
            ShortcutMenuItem {
                text: i18n.t("Properties...")
                onTriggered: mainWindow.openWindow("properties")
            }
        }

        Menu {
            title: "&Video"
            ShortcutMenuItem {
                text: i18n.t("Open Video...")
                onTriggered: videoDialog.open()
            }
            ShortcutMenuItem {
                text: i18n.t("Use Dummy Video")
                onTriggered: videoVM.openDummyVideo(0, 0, 0, 0)
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Close Video")
                enabled: videoVM.hasVideo
                onTriggered: videoVM.closeVideo()
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Jump to Active Line")
                shortcutText: "Ctrl+J"
                enabled: videoVM.hasVideo
                onTriggered: videoVM.seekToActiveLine()
            }
        }

        Menu {
            title: "&Audio"
            ShortcutMenuItem {
                text: i18n.t("Open Audio...")
                onTriggered: audioDialog.open()
            }
            ShortcutMenuItem {
                text: i18n.t("Open Audio from Video")
                enabled: videoVM.hasVideo && videoVM.filePath.length > 0
                onTriggered: audioVM.openLocalPath(videoVM.filePath)
            }
            ShortcutMenuItem {
                text: i18n.t("Use Dummy Audio")
                onTriggered: audioVM.openDummyAudio()
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Close Audio")
                enabled: audioVM.hasAudio
                onTriggered: audioVM.closeAudio()
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Play Line")
                shortcutText: "Ctrl+Space"
                enabled: audioVM.hasAudio
                onTriggered: audioVM.playSelection()
            }
            ShortcutMenuItem {
                text: i18n.t("Stop")
                enabled: audioVM.hasAudio && audioVM.isPlaying
                onTriggered: audioVM.stop()
            }
        }

        Menu {
            title: "&Tools"
            ShortcutMenuItem {
                text: i18n.t("Preferences...")
                onTriggered: mainWindow.openWindow("preferences")
            }
        }

        Menu {
            title: "&Help"
            ShortcutMenuItem {
                text: i18n.t("About Aegisub")
                onTriggered: mainWindow.openWindow("about")
            }
        }
    }

    // ---- Dialogs ---------------------------------------------------------

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

    FileDialog {
        id: videoDialog
        title: "Open video file"
        nameFilters: ["Video files (*.mp4 *.mkv *.avi *.webm *.mov *.ts)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: videoVM.openVideo(selectedFile)
    }

    FileDialog {
        id: audioDialog
        title: "Open audio file"
        nameFilters: ["Audio/Video files (*.wav *.flac *.mp3 *.aac *.m4a *.ogg *.opus *.mp4 *.mkv *.avi)",
                      "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: audioVM.openAudio(selectedFile)
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

    // ---- Workspace -------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Error banner (e.g. failed open/save); closable.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 32 : 0
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

        // Desktop workspace: draggable vertical split. The top media row holds
        // the video preview and the audio waveform side by side; each one fills
        // the whole row when the other is hidden, and the row collapses
        // completely (back to the M1 layout) when both are closed.
        SplitView {
            id: mainSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            handle: Rectangle {
                implicitHeight: 6
                color: SplitHandle.hovered ? "#3a3a3a" : "#1e1e1e"
            }

            SplitView {
                id: mediaSplit
                visible: videoVM.hasVideo || audioVM.hasAudio
                SplitView.preferredHeight: 300
                SplitView.minimumHeight: 180
                orientation: Qt.Horizontal

                handle: Rectangle {
                    implicitWidth: 6
                    color: SplitHandle.hovered ? "#3a3a3a" : "#1e1e1e"
                }

                Loader {
                    id: videoPanelLoader
                    source: "qrc:/VideoPanel.qml"
                    visible: videoVM.hasVideo
                    SplitView.preferredWidth: 460
                    SplitView.minimumWidth: 320
                    SplitView.fillWidth: !audioVM.hasAudio
                }

                Loader {
                    id: audioPanelLoader
                    source: "qrc:/AudioPanel.qml"
                    visible: audioVM.hasAudio
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 320
                }
            }

            Loader {
                id: gridLoader
                source: "qrc:/SubtitleGrid.qml"
                SplitView.fillHeight: true
                SplitView.minimumHeight: 160
            }

            Loader {
                id: editBoxLoader
                source: "qrc:/EditBox.qml"
                SplitView.preferredHeight: 185
                SplitView.minimumHeight: 150
            }
        }
    }

    // ---- Compact status bar ---------------------------------------------

    footer: Rectangle {
        height: 24
        color: "#252525"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

            Label {
                text: gridModel.lineCount + " " + i18n.t("lines")
                      + (gridModel.selectionCount > 1
                         ? " | " + gridModel.selectionCount + " " + i18n.t("selected")
                         : "")
                font.pixelSize: 11
                color: "#9a9a9a"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: documentVM.encoding + " | Aegisub " + appInfo.version
                font.pixelSize: 11
                color: "#8a8a8a"
            }
        }
    }
}
