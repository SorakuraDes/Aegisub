import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1000
    height: 700
    minimumWidth: 720
    minimumHeight: 570
    title: documentVM.displayName + (documentVM.dirty ? " *" : "") + " - Aegisub Qt6"

    color: themeVM.surface
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    property var aboutWindow: null
    property var preferencesWindow: null
    property var propertiesWindow: null
    property var searchWindow: null
    property var stylesWindow: null
    property var shiftTimesWindow: null
    property var trackPickerWindow: null

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

    // Non-modal Shift Times window (one instance, reopened from Subtitle menu).
    function openShiftTimes() {
        if (shiftTimesWindow === null) {
            var component = Qt.createComponent("qrc:/ShiftTimesDialog.qml")
            if (component.status !== Component.Ready) {
                console.error("Failed to load ShiftTimesDialog.qml", component.errorString())
                return
            }
            shiftTimesWindow = component.createObject(null)
        }
        shiftTimesWindow.show()
        shiftTimesWindow.raise()
        shiftTimesWindow.requestActivate()
    }

    // Qt track picker for multi-track video/audio containers. The view models
    // probe the file first and only ask for this window when there is a real
    // choice (see TrackPickerDialog.qml).
    function openTrackPicker(path, kind, tracks) {
        if (trackPickerWindow === null) {
            var component = Qt.createComponent("qrc:/TrackPickerDialog.qml")
            if (component.status !== Component.Ready) {
                console.error("Failed to load TrackPickerDialog.qml", component.errorString())
                return
            }
            trackPickerWindow = component.createObject(null)
        }
        trackPickerWindow.openFor(path, kind, tracks)
    }

    Connections {
        target: videoVM
        function onTrackSelectionRequired(path, kind, tracks) {
            mainWindow.openTrackPicker(path, kind, tracks)
        }
    }

    Connections {
        target: audioVM
        function onTrackSelectionRequired(path, kind, tracks) {
            mainWindow.openTrackPicker(path, kind, tracks)
        }
    }

    function closeChildWindows() {
        resampleDialog.close()
        if (searchWindow !== null) {
            searchWindow.close()
            searchWindow = null
        }
        if (stylesWindow !== null) {
            stylesWindow.close()
            stylesWindow = null
        }
        if (shiftTimesWindow !== null) {
            shiftTimesWindow.close()
            shiftTimesWindow = null
        }
        if (trackPickerWindow !== null) {
            trackPickerWindow.close()
            trackPickerWindow = null
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

    // Called by main.cpp's MainShortcutFilter (Ctrl+Shift+S; see the comment
    // there for why this is not a QML Shortcut).
    function openSaveAsDialog() {
        saveAsDialog.open()
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

    readonly property bool hasSelection: gridModel.selectionCount > 0
    readonly property bool canSplit: videoVM.hasVideo && hasSelection
    readonly property bool canJoin: gridModel.selectionCount >= 2
    readonly property bool canJump: videoVM.hasVideo && gridModel.activeRow >= 0

    // Window-scoped shortcuts cannot fire in the auxiliary windows. Also
    // check the actual editor type, including writable SpinBox internals.
    function textInputFocused() {
        for (var item = activeFocusItem; item; item = item.parent) {
            if (item instanceof TextInput || item instanceof TextEdit || item.isEditBoxField === true)
                return true
        }
        return false
    }

    function findNext() {
        if (searchVM.query.length === 0)
            openSearch(false)
        else
            searchVM.findNext()
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
    // Ctrl+S / Ctrl+Shift+S are handled by main.cpp's MainShortcutFilter (the
    // QML Shortcut matcher cannot tell them apart for letter keys).
    Shortcut { sequence: StandardKey.Quit; onActivated: mainWindow.requestAction("quit") }
    Shortcut { sequence: StandardKey.Find; onActivated: mainWindow.openSearch(false) }
    Shortcut { sequence: StandardKey.Replace; onActivated: mainWindow.openSearch(true) }
    // Shift Times (Aegisub's subtitle/shift times; Ctrl+I matches the wx frontend).
    Shortcut { sequence: "Ctrl+I"; onActivated: mainWindow.openShiftTimes() }

    // Row actions, kept away from the text editors above.
    Shortcut {
        sequence: "Delete"
        enabled: !mainWindow.textInputFocused() && gridModel.selectionCount > 0
        onActivated: gridModel.deleteSelectedRows()
    }
    Shortcut { sequence: "Ctrl+X"; enabled: !mainWindow.textInputFocused() && mainWindow.hasSelection; onActivated: gridModel.cutSelectedRows() }
    Shortcut { sequence: "Ctrl+C"; enabled: !mainWindow.textInputFocused() && mainWindow.hasSelection; onActivated: gridModel.copySelectedRows() }
    Shortcut { sequence: "Ctrl+V"; enabled: !mainWindow.textInputFocused(); onActivated: gridModel.pasteRows() }
    Shortcut { sequence: "Alt+Up"; enabled: !mainWindow.textInputFocused() && mainWindow.hasSelection; onActivated: gridModel.moveSelectedRows(-1) }
    Shortcut { sequence: "Alt+Down"; enabled: !mainWindow.textInputFocused() && mainWindow.hasSelection; onActivated: gridModel.moveSelectedRows(1) }
    Shortcut { sequence: "F3"; onActivated: mainWindow.findNext() }
    Shortcut { sequence: "Alt+O"; onActivated: mainWindow.openWindow("preferences") }
    Shortcut { sequence: "Ctrl+1"; enabled: mainWindow.canJump; onActivated: videoVM.seekToActiveLine() }
    Shortcut { sequence: "Ctrl+2"; enabled: mainWindow.canJump; onActivated: videoVM.seekToActiveLineEnd() }
    Shortcut { sequence: "Ctrl+3"; enabled: mainWindow.canSplit; onActivated: gridModel.snapToVideo(true) }
    Shortcut { sequence: "Ctrl+4"; enabled: mainWindow.canSplit; onActivated: gridModel.snapToVideo(false) }
    // The C++ key filter distinguishes Ctrl+D from Ctrl+Shift+D.
    function splitBeforeFrame() { if (canSplit) gridModel.splitAtVideoFrame(true) }
    function splitAfterFrame() { if (canSplit) gridModel.splitAtVideoFrame(false) }

    Shortcut {
        sequence: StandardKey.SelectAll
        enabled: !mainWindow.textInputFocused()
        onActivated: gridModel.selectAll()
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
            implicitHeight: themeVM.menuBarHeight
            color: themeVM.surfaceContainer
        }

        delegate: MenuBarItem {
            id: barItem
            implicitHeight: themeVM.menuBarHeight
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
                color: barItem.highlighted ? Material.accent : themeVM.onSurface
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: barItem.highlighted ? themeVM.surfaceContainerHigh : "transparent"
            }
        }

        Menu {
            title: i18n.t("File")
            width: 310
            ShortcutMenuItem { text: i18n.t("New Subtitles"); shortcutText: "Ctrl+N"; onTriggered: mainWindow.requestAction("new") }
            ShortcutMenuItem { text: i18n.t("Open Subtitles..."); shortcutText: "Ctrl+O"; onTriggered: mainWindow.requestAction("open") }
            ShortcutMenuItem { text: i18n.t("Save Subtitles"); shortcutText: "Ctrl+S"; onTriggered: mainWindow.saveDocument() }
            ShortcutMenuItem { text: i18n.t("Save Subtitles as..."); shortcutText: "Ctrl+Shift+S"; onTriggered: saveAsDialog.open() }
            MenuSeparator {}
            ShortcutMenuItem { text: i18n.t("Properties..."); onTriggered: mainWindow.openWindow("properties") }
            MenuSeparator {}
            ShortcutMenuItem { text: i18n.t("Exit"); shortcutText: "Ctrl+Q"; onTriggered: mainWindow.requestAction("quit") }
        }

        Menu {
            title: i18n.t("Edit")
            width: 295
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
            ShortcutMenuItem { text: i18n.t("Cut Lines"); shortcutText: "Ctrl+X"; enabled: mainWindow.hasSelection; onTriggered: gridModel.cutSelectedRows() }
            ShortcutMenuItem { text: i18n.t("Copy Lines"); shortcutText: "Ctrl+C"; enabled: mainWindow.hasSelection; onTriggered: gridModel.copySelectedRows() }
            ShortcutMenuItem { text: i18n.t("Paste Lines"); shortcutText: "Ctrl+V"; onTriggered: gridModel.pasteRows() }
            MenuSeparator {}
            ShortcutMenuItem { text: i18n.t("Find..."); shortcutText: "Ctrl+F"; onTriggered: mainWindow.openSearch(false) }
            ShortcutMenuItem { text: i18n.t("Find Next"); shortcutText: "F3"; onTriggered: mainWindow.findNext() }
            ShortcutMenuItem { text: i18n.t("Find and Replace..."); shortcutText: "Ctrl+H"; onTriggered: mainWindow.openSearch(true) }
        }

        Menu {
            title: i18n.t("Subtitle")
            width: 365
            delegate: ShortcutMenuItem {}
            ShortcutMenuItem { text: i18n.t("Styles Manager..."); onTriggered: mainWindow.openStyles() }
            ShortcutMenuItem { text: i18n.t("Resample Resolution..."); onTriggered: resampleDialog.open() }
            MenuSeparator {}
            Menu {
                title: i18n.t("Insert Lines")
                width: 210
                ShortcutMenuItem { text: i18n.t("Before Current"); onTriggered: gridModel.insertBefore(gridModel.activeRow) }
                ShortcutMenuItem { text: i18n.t("After Current"); onTriggered: gridModel.insertAfter(gridModel.activeRow) }
            }
            ShortcutMenuItem {
                text: i18n.t("Duplicate Lines")
                enabled: mainWindow.hasSelection
                onTriggered: gridModel.duplicateSelectedRows()
            }
            ShortcutMenuItem {
                text: i18n.t("Split lines before current frame")
                shortcutText: "Ctrl+D"
                enabled: mainWindow.canSplit
                onTriggered: mainWindow.splitBeforeFrame()
            }
            ShortcutMenuItem {
                text: i18n.t("Split lines after current frame")
                shortcutText: "Ctrl+Shift+D"
                enabled: mainWindow.canSplit
                onTriggered: mainWindow.splitAfterFrame()
            }
            ShortcutMenuItem {
                text: i18n.t("Delete Lines")
                shortcutText: "Delete"
                enabled: mainWindow.hasSelection
                onTriggered: gridModel.deleteSelectedRows()
            }
            MenuSeparator {}
            Menu {
                id: joinLinesMenu
                title: i18n.t("Join Lines")
                width: 210
                ShortcutMenuItem { text: i18n.t("Concatenate"); enabled: mainWindow.canJoin; onTriggered: gridModel.joinSelectedRows(true) }
                ShortcutMenuItem { text: i18n.t("Keep First"); enabled: mainWindow.canJoin; onTriggered: gridModel.joinSelectedRows(false) }
            }
            MenuSeparator {}
            Menu {
                title: i18n.t("Sort All Lines")
                width: 210
                ShortcutMenuItem { text: i18n.t("Start Time"); onTriggered: gridModel.sortAllLines(0) }
                ShortcutMenuItem { text: i18n.t("End Time"); onTriggered: gridModel.sortAllLines(1) }
                ShortcutMenuItem { text: i18n.t("Style Name"); onTriggered: gridModel.sortAllLines(2) }
                ShortcutMenuItem { text: i18n.t("Actor Name"); onTriggered: gridModel.sortAllLines(3) }
                ShortcutMenuItem { text: i18n.t("Effect"); onTriggered: gridModel.sortAllLines(4) }
                ShortcutMenuItem { text: i18n.t("Text"); onTriggered: gridModel.sortAllLines(6) }
                ShortcutMenuItem { text: i18n.t("Layer"); onTriggered: gridModel.sortAllLines(5) }
            }
            ShortcutMenuItem { text: i18n.t("Select All"); shortcutText: "Ctrl+A"; onTriggered: gridModel.selectAll() }
        }

        Menu {
            title: i18n.t("Timing")
            width: 290
            delegate: ShortcutMenuItem {}
            ShortcutMenuItem { text: i18n.t("Shift Times..."); shortcutText: "Ctrl+I"; onTriggered: mainWindow.openShiftTimes() }
            MenuSeparator {}
            ShortcutMenuItem { text: i18n.t("Snap Start to Video"); shortcutText: "Ctrl+3"; enabled: mainWindow.canSplit; onTriggered: gridModel.snapToVideo(true) }
            ShortcutMenuItem { text: i18n.t("Snap End to Video"); shortcutText: "Ctrl+4"; enabled: mainWindow.canSplit; onTriggered: gridModel.snapToVideo(false) }
            MenuSeparator {}
            Menu {
                title: i18n.t("Make Times Continuous")
                width: 210
                ShortcutMenuItem { text: i18n.t("Change Start"); enabled: mainWindow.canJoin; onTriggered: gridModel.makeTimesContinuous(true) }
                ShortcutMenuItem { text: i18n.t("Change End"); enabled: mainWindow.canJoin; onTriggered: gridModel.makeTimesContinuous(false) }
            }
        }

        Menu {
            title: i18n.t("Video")
            width: 300
            ShortcutMenuItem { text: i18n.t("Open Video..."); onTriggered: videoDialog.open() }
            ShortcutMenuItem { text: i18n.t("Close Video"); enabled: videoVM.hasVideo; onTriggered: videoVM.closeVideo() }
            ShortcutMenuItem { text: i18n.t("Use Dummy Video..."); onTriggered: videoVM.openDummyVideo(0, 0, 0, 0) }
            MenuSeparator {}
            ShortcutMenuItem { text: i18n.t("Jump Video to Start"); shortcutText: "Ctrl+1"; enabled: mainWindow.canJump; onTriggered: videoVM.seekToActiveLine() }
            ShortcutMenuItem { text: i18n.t("Jump Video to End"); shortcutText: "Ctrl+2"; enabled: mainWindow.canJump; onTriggered: videoVM.seekToActiveLineEnd() }
        }

        Menu {
            title: i18n.t("Audio")
            width: 300
            ShortcutMenuItem { text: i18n.t("Open Audio File..."); onTriggered: audioDialog.open() }
            ShortcutMenuItem {
                text: i18n.t("Open Audio from Video")
                enabled: videoVM.hasVideo && videoVM.filePath.length > 0
                onTriggered: audioVM.openLocalPath(videoVM.filePath)
            }
            ShortcutMenuItem { text: i18n.t("Select Audio Track..."); enabled: audioVM.hasAudio; onTriggered: audioVM.openAudioTrackPicker() }
            ShortcutMenuItem { text: i18n.t("Close Audio"); enabled: audioVM.hasAudio; onTriggered: audioVM.closeAudio() }
            ShortcutMenuItem { text: i18n.t("Use Dummy Audio"); onTriggered: audioVM.openDummyAudio() }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Spectrum Display")
                checkable: true
                autoExclusive: true
                checked: audioVM.spectrumMode
                onTriggered: audioVM.spectrumMode = true
            }
            ShortcutMenuItem {
                text: i18n.t("Waveform Display")
                checkable: true
                autoExclusive: true
                checked: !audioVM.spectrumMode
                onTriggered: audioVM.spectrumMode = false
            }
            MenuSeparator {}
            ShortcutMenuItem { text: i18n.t("Play Line"); shortcutText: "Ctrl+Space"; enabled: audioVM.hasAudio; onTriggered: audioVM.playSelection() }
            ShortcutMenuItem { text: i18n.t("Stop"); enabled: audioVM.hasAudio && audioVM.isPlaying; onTriggered: audioVM.stop() }
            ShortcutMenuItem {
                text: i18n.t("Follow Playhead")
                checkable: true
                checked: audioVM.followPlayhead
                onTriggered: audioVM.followPlayhead = checked
            }
        }

        Menu {
            title: i18n.t("View")
            width: 240
            ShortcutMenuItem {
                icon.source: "qrc:/icons/settings.svg"
                text: i18n.t("Options...")
                shortcutText: "Alt+O"
                onTriggered: mainWindow.openWindow("preferences")
            }
            MenuSeparator {}
            ShortcutMenuItem {
                text: i18n.t("Dark Mode")
                checkable: true
                checked: themeVM.darkMode
                onTriggered: themeVM.darkMode = checked
            }
        }

        Menu {
            title: i18n.t("Help")
            ShortcutMenuItem {
                text: i18n.t("About")
                onTriggered: mainWindow.openWindow("about")
            }
        }
    }

    // ---- Dialogs ---------------------------------------------------------

    ResampleDialog { id: resampleDialog }

    FileDialog {
        id: openDialog
        title: "Open subtitle file"
        nameFilters: ["Subtitle files (*.ass *.ssa *.srt)",
                      "ASS subtitles (*.ass *.ssa)",
                      "SRT subtitles (*.srt)",
                      "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: documentVM.openFile(selectedFile)
    }

    FileDialog {
        id: saveAsDialog
        title: "Save subtitle file as"
        nameFilters: ["Subtitle files (*.ass *.ssa *.srt)",
                      "ASS subtitles (*.ass *.ssa)",
                      "SRT subtitles (*.srt)",
                      "All files (*)"]
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
            Layout.preferredHeight: visible ? themeVM.controlHeight : 0
            visible: documentVM.lastError.length > 0
            color: themeVM.surfaceContainerHighest
            radius: themeVM.radiusSmall

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: documentVM.lastError
                    color: themeVM.danger
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                CompactIconButton {
                    text: i18n.t("Dismiss error")
                    icon.source: "qrc:/icons/close.svg"
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
                color: SplitHandle.hovered ? themeVM.outlineVariant : themeVM.surface
            }

            SplitView {
                id: mediaSplit
                visible: videoVM.hasVideo || audioVM.hasAudio
                SplitView.preferredHeight: 300
                SplitView.minimumHeight: 180
                orientation: Qt.Horizontal

                handle: Rectangle {
                    implicitWidth: 6
                    color: SplitHandle.hovered ? themeVM.outlineVariant : themeVM.surface
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
        height: themeVM.statusBarHeight
        color: themeVM.surfaceContainer

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
                color: themeVM.onSurfaceVariant
            }

            Item { Layout.fillWidth: true }

            Label {
                text: documentVM.encoding + " | Aegisub " + appInfo.version
                font.pixelSize: 11
                color: themeVM.onSurfaceVariant
            }
        }
    }
}
