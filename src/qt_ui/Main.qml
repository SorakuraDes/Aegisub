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
    property var selectLinesWindow: null

    function openSelectLines() {
        if (selectLinesWindow === null) {
            const component = Qt.createComponent("qrc:/SelectLinesDialog.qml")
            if (component.status !== Component.Ready) {
                console.error("Failed to load SelectLinesDialog.qml", component.errorString())
                return
            }
            selectLinesWindow = component.createObject(mainWindow)
            if (selectLinesWindow === null) return
        }
        selectLinesWindow.openForSelectLines()
        raiseAndActivate(selectLinesWindow)
    }

    function openPasteOver() { if (hasSelection && !pasteOverDialog.visible) pasteOverDialog.open() }
    function openAttachments() { attachmentsDialog.open() }
    property var fontsCollectorDialog: null
    function openFontsCollector() {
        if (fontsCollectorDialog === null) {
            const component = Qt.createComponent("qrc:/FontsCollectorDialog.qml")
            if (component.status !== Component.Ready) {
                console.error(component.errorString())
                return
            }
            fontsCollectorDialog = component.createObject(mainWindow, { transientParent: mainWindow })
        }
        if (fontsCollectorDialog !== null) {
            fontsCollectorDialog.open()
            raiseAndActivate(fontsCollectorDialog)
        }
    }
    function pasteGridLines() { gridModel.pasteRows() }

    AttachmentsDialog { id: attachmentsDialog }
    PasteOverDialog { id: pasteOverDialog; transientParent: mainWindow }
    property var trackPickerWindow: null
    property var logWindow: null
    property int workspaceLayout: 3
    readonly property bool showVideoPanel: videoVM.hasVideo && (workspaceLayout === 1 || workspaceLayout === 3)
    readonly property bool showAudioPanel: audioVM.hasAudio && (workspaceLayout === 2 || workspaceLayout === 3)
    property string recentSubtitle: ""

    // Pending File-menu action while the unsaved-changes dialog is up, and the
    // action to resume once a (possibly Save As) save finished.
    property string pendingAction: ""
    property string pendingAfterSave: ""
    // Set once the user chose to discard; lets close() get through onClosing.
    property bool forceClose: false
    property bool pendingCloseAfterAutomation: false

    // ---- Windows ---------------------------------------------------------

    function windowFor(kind) {
        if (kind === "about")
            return aboutWindow
        if (kind === "preferences")
            return preferencesWindow
        return kind === "log" ? logWindow : propertiesWindow
    }

    function setWindow(kind, win) {
        if (kind === "about")
            aboutWindow = win
        else if (kind === "preferences")
            preferencesWindow = win
        else if (kind === "log")
            logWindow = win
        else
            propertiesWindow = win
    }

    function raiseAndActivate(win) {
        if (nativeWindowRaise) win.raise()
        win.requestActivate()
    }

    function openWindow(kind) {
        var existing = windowFor(kind)
        if (existing !== null) {
            existing.show()
            raiseAndActivate(existing)
            return
        }

        var url = "qrc:/" + (kind === "about" ? "AboutWindow.qml"
                            : kind === "preferences" ? "PreferencesWindow.qml"
                            : kind === "log" ? "LogWindow.qml"
                            : "PropertiesWindow.qml")
        var component = Qt.createComponent(url)
        if (component.status !== Component.Ready) {
            console.error("Failed to load", url, component.errorString())
            return
        }
        var win = component.createObject(null)
        setWindow(kind, win)
        win.show()
        raiseAndActivate(win)
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
        raiseAndActivate(searchWindow)
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
        raiseAndActivate(stylesWindow)
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
        raiseAndActivate(shiftTimesWindow)
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

    property bool skipLinkedVideo: false
    property bool skipLinkedAudio: false
    function openLinkedMedia(videoPath, audioPath, zoomPercent, frame, skipVideo, skipAudio) {
        if (!skipVideo) {
            if (videoPath.length === 0) {
                if (videoVM.hasVideo) videoVM.closeVideo()
            } else if (videoPath === videoVM.filePath || videoVM.openLocalPath(videoPath)) {
                videoVM.zoomPercent = zoomPercent
                if (frame >= 0) videoVM.seekFrame(frame)
            }
        }
        if (!skipAudio) {
            if (audioPath.length === 0) {
                if (audioVM.hasAudio) audioVM.closeAudio()
            } else if (audioPath !== audioVM.filePath) {
                audioVM.openLocalPath(audioPath)
            }
        }
    }

    Connections {
        target: documentVM
        function onLinkedMediaRequested(videoPath, audioPath, ask, zoomPercent, frame) {
            const skipVideo = mainWindow.skipLinkedVideo
            const skipAudio = mainWindow.skipLinkedAudio
            const videoMatches = skipVideo || (videoPath === videoVM.filePath && (videoPath.length > 0 || !videoVM.hasVideo))
            const audioMatches = skipAudio || (audioPath === audioVM.filePath && (audioPath.length > 0 || !audioVM.hasAudio))
            if (ask && (!videoMatches || !audioMatches)) {
                linkedMediaDialog.videoPath = videoPath
                linkedMediaDialog.audioPath = audioPath
                linkedMediaDialog.zoomPercent = zoomPercent
                linkedMediaDialog.frame = frame
                linkedMediaDialog.skipVideo = skipVideo
                linkedMediaDialog.skipAudio = skipAudio
                linkedMediaDialog.open()
            } else {
                mainWindow.openLinkedMedia(videoPath, audioPath, zoomPercent, frame, skipVideo, skipAudio)
            }
        }
    }

    function closeChildWindows() {
        resampleDialog.close()
        jumpDialog.close()
        detailsDialog.close()
        attachmentsDialog.close()
        if (fontsCollectorDialog !== null) fontsCollectorDialog.close()
        pasteOverDialog.close()
        if (selectLinesWindow !== null) selectLinesWindow.close()
        if (logWindow !== null) logWindow.close()
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
        } else if (action === "recent") {
            documentVM.openLocalPath(recentSubtitle)
        } else if (action === "quit") {
            forceClose = true
            mainWindow.close()
        }
        pendingAction = ""
    }

    // Used by File -> New / Open and the window close box: never throw away
    // unsaved changes without asking.
    function requestAction(action, path) {
        if (action === "recent")
            recentSubtitle = path
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

    // In-window dialogs share the main QQuickWindow.
    readonly property bool hotkeysBlocked: unsavedDialog.visible || linkedMediaDialog.visible
        || jumpDialog.visible || detailsDialog.visible || resampleDialog.visible
        || attachmentsDialog.visible || languageDialog.visible || exportSubtitlesDialog.visible
        || automationManagerDialog.visible || automationErrorDialog.visible || automationForm.visible
        || automationProgress.visible || openDialog.visible || saveAsDialog.visible
        || videoDialog.visible || audioDialog.visible

    function findNext() {
        if (searchVM.query.length === 0)
            openSearch(false)
        else
            searchVM.findNext()
    }

    function resumeCloseAfterAutomation() {
        if (!pendingCloseAfterAutomation || automationVM.busy)
            return
        pendingCloseAfterAutomation = false
        mainWindow.close()
    }

    onClosing: function(close) {
        if (fontsCollectorVM.running) {
            close.accepted = false
            return
        }
        if (automationVM.busy) {
            close.accepted = false
            if (!pendingCloseAfterAutomation) {
                pendingCloseAfterAutomation = true
                automationVM.cancelOperation()
                automationForm.dismissRequest()
                automationPicker.dismissRequest()
            }
            return
        }
        pendingCloseAfterAutomation = false
        if (!forceClose && documentVM.dirty) {
            close.accepted = false
            requestAction("quit")
            return
        }
        // Do not leave the auxiliary windows (search, styles, ...) open: they
        // would keep the application alive after the main window closes.
        closeChildWindows()
    }

    // ---- Compact desktop menu bar ----------------------------------------

    component CommandMenuItem: ShortcutMenuItem {
        property string commandId
        property string hotkeyContext: "Default"
        shortcutText: {
            hotkeyVM.revision
            return hotkeyVM.menuShortcut(commandId, hotkeyContext)
        }
        onTriggered: hotkeyVM.dispatch(commandId)
    }

    function showCommandUi(commandId) {
        switch (commandId) {
        case "subtitle/new": mainWindow.requestAction("new"); break
        case "subtitle/open": mainWindow.requestAction("open"); break
        case "subtitle/save": mainWindow.saveDocument(); break
        case "subtitle/save/as": saveAsDialog.open(); break
        case "tool/export": exportSubtitlesDialog.open(); break
        case "subtitle/properties": mainWindow.openWindow("properties"); break
        case "subtitle/attachment": mainWindow.openAttachments(); break
        case "tool/font_collector": mainWindow.openFontsCollector(); break
        case "app/exit": mainWindow.close(); break
        case "edit/line/paste/over": mainWindow.openPasteOver(); break
        case "subtitle/find": mainWindow.openSearch(false); break
        case "subtitle/find/next": mainWindow.findNext(); break
        case "edit/find_replace": mainWindow.openSearch(true); break
        case "tool/style/manager": mainWindow.openStyles(); break
        case "tool/resampleres": resampleDialog.open(); break
        case "tool/line/select": mainWindow.openSelectLines(); break
        case "time/shift": mainWindow.openShiftTimes(); break
        case "video/open": videoDialog.open(); break
        case "video/details": detailsDialog.open(); break
        case "video/jump": jumpDialog.open(); break
        case "audio/open": audioDialog.open(); break
        case "am/manager": Qt.callLater(() => automationManagerDialog.open()); break
        case "am/reload/autoload": Qt.callLater(() => automationVM.reloadAutoload()); break
        case "app/language": languageDialog.open(); break
        case "app/options": mainWindow.openWindow("preferences"); break
        case "app/about": mainWindow.openWindow("about"); break
        case "app/log": mainWindow.openWindow("log"); break
        case "app/display/subs": mainWindow.workspaceLayout = 0; break
        case "app/display/video_subs": mainWindow.workspaceLayout = 1; break
        case "app/display/audio_subs": mainWindow.workspaceLayout = 2; break
        case "app/display/full": mainWindow.workspaceLayout = 3; break
        }
    }

    menuBar: MenuBar {
        id: mainMenuBar
        objectName: "mainMenuBar"
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
                text: barItem.text.length > 0
                      ? barItem.text
                      : (barItem.action ? barItem.action.text : "")
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
            delegate: ShortcutMenuItem {}
            CommandMenuItem { text: i18n.t("New Subtitles"); commandId: "subtitle/new"}
            CommandMenuItem { text: i18n.t("Open Subtitles..."); commandId: "subtitle/open"}
            CommandMenuItem { text: i18n.t("Save Subtitles"); commandId: "subtitle/save"}
            CommandMenuItem { text: i18n.t("Save Subtitles as..."); commandId: "subtitle/save/as"}
            CommandMenuItem {
                objectName: "exportSubtitlesMenuItem"
                text: i18n.t("&Export Subtitles...")
                enabled: !automationVM.busy
                commandId: "tool/export"
            }
            RecentMenu { objectName: "recentSubtitlesMenu"; paths: mruVM.subtitles; onOpenPath: path => mainWindow.requestAction("recent", path) }
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Properties..."); commandId: "subtitle/properties"}
            CommandMenuItem { objectName: "attachmentsMenuItem"; text: i18n.t("Attachments..."); commandId: "subtitle/attachment"}
            CommandMenuItem { objectName: "fontsCollectorMenuItem"; text: i18n.t("Fonts Collector..."); commandId: "tool/font_collector"}
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Exit"); commandId: "app/exit"}
        }

        Menu {
            title: i18n.t("Edit")
            width: 295
            CommandMenuItem {
                // Dynamic label in the style of the wxWidgets frontend:
                // "Undo <what will be undone>".
                text: undoVM.canUndo && undoVM.undoDescription.length > 0
                      ? i18n.t("Undo") + " " + undoVM.undoDescription
                      : i18n.t("Undo")
                enabled: undoVM.canUndo
                commandId: "edit/undo"
            }
            CommandMenuItem {
                text: undoVM.canRedo && undoVM.redoDescription.length > 0
                      ? i18n.t("Redo") + " " + undoVM.redoDescription
                      : i18n.t("Redo")
                enabled: undoVM.canRedo
                commandId: "edit/redo"
            }
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Cut Lines"); enabled: mainWindow.hasSelection; commandId: "edit/line/cut"}
            CommandMenuItem { text: i18n.t("Copy Lines"); enabled: mainWindow.hasSelection; commandId: "edit/line/copy"}
            CommandMenuItem { text: i18n.t("Paste Lines"); commandId: "edit/line/paste"}
            CommandMenuItem { objectName: "pasteOverMenuItem"; text: i18n.t("Paste Lines Over..."); enabled: mainWindow.hasSelection; commandId: "edit/line/paste/over"}
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Find..."); commandId: "subtitle/find"}
            CommandMenuItem { text: i18n.t("Find Next"); commandId: "subtitle/find/next"}
            CommandMenuItem { text: i18n.t("Find and Replace..."); commandId: "edit/find_replace"}
        }

        Menu {
            title: i18n.t("Subtitle")
            width: 365
            delegate: ShortcutMenuItem {}
            CommandMenuItem { text: i18n.t("Styles Manager..."); commandId: "tool/style/manager"}
            CommandMenuItem { text: i18n.t("Resample Resolution..."); commandId: "tool/resampleres"}
            MenuSeparator {}
            Menu {
                title: i18n.t("Insert Lines")
                width: 210
                CommandMenuItem { text: i18n.t("Before Current"); commandId: "subtitle/insert/before"}
                CommandMenuItem { text: i18n.t("After Current"); commandId: "subtitle/insert/after"}
                CommandMenuItem { text: i18n.t("Before Current, at Video Time"); enabled: videoVM.hasVideo; commandId: "subtitle/insert/before/videotime"}
                CommandMenuItem { text: i18n.t("After Current, at Video Time"); enabled: videoVM.hasVideo; commandId: "subtitle/insert/after/videotime"}
            }
            CommandMenuItem {
                text: i18n.t("Duplicate Lines")
                enabled: mainWindow.hasSelection
                commandId: "edit/line/duplicate"
            }
            CommandMenuItem {
                text: i18n.t("Split lines before current frame")
                enabled: mainWindow.canSplit
                commandId: "edit/line/split/before"
            }
            CommandMenuItem {
                text: i18n.t("Split lines after current frame")
                enabled: mainWindow.canSplit
                commandId: "edit/line/split/after"
            }
            CommandMenuItem {
                text: i18n.t("Delete Lines")
                enabled: mainWindow.hasSelection
                commandId: "edit/line/delete"
            }
            MenuSeparator {}
            Menu {
                id: joinLinesMenu
                title: i18n.t("Join Lines")
                width: 210
                CommandMenuItem { text: i18n.t("Concatenate"); enabled: mainWindow.canJoin; commandId: "edit/line/join/concatenate"}
                CommandMenuItem { text: i18n.t("Keep First"); enabled: mainWindow.canJoin; commandId: "edit/line/join/keep_first"}
            }
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Open all Folds"); enabled: gridModel.hasFolds; commandId: "grid/fold/open_all"}
            CommandMenuItem { text: i18n.t("Close all Folds"); enabled: gridModel.hasFolds; commandId: "grid/fold/close_all"}
            CommandMenuItem { text: i18n.t("Clear all Folds"); enabled: gridModel.hasFolds; commandId: "grid/fold/clear_all"}
            MenuSeparator {}
            Menu {
                title: i18n.t("Sort All Lines")
                width: 210
                CommandMenuItem { text: i18n.t("Start Time"); commandId: "grid/sort/start"}
                CommandMenuItem { text: i18n.t("End Time"); commandId: "grid/sort/end"}
                CommandMenuItem { text: i18n.t("Style Name"); commandId: "grid/sort/style"}
                CommandMenuItem { text: i18n.t("Actor Name"); commandId: "grid/sort/actor"}
                CommandMenuItem { text: i18n.t("Effect"); commandId: "grid/sort/effect"}
                CommandMenuItem { text: i18n.t("Text"); commandId: "grid/sort/text"}
                CommandMenuItem { text: i18n.t("Stripped Text"); commandId: "grid/sort/text_stripped"}
                CommandMenuItem { text: i18n.t("Layer"); commandId: "grid/sort/layer"}
            }
            Menu {
                title: i18n.t("Sort Selected Lines")
                width: 210
                CommandMenuItem { text: i18n.t("Start Time"); enabled: mainWindow.canJoin; commandId: "grid/sort/start/selected"}
                CommandMenuItem { text: i18n.t("End Time"); enabled: mainWindow.canJoin; commandId: "grid/sort/end/selected"}
                CommandMenuItem { text: i18n.t("Style Name"); enabled: mainWindow.canJoin; commandId: "grid/sort/style/selected"}
                CommandMenuItem { text: i18n.t("Actor Name"); enabled: mainWindow.canJoin; commandId: "grid/sort/actor/selected"}
                CommandMenuItem { text: i18n.t("Effect"); enabled: mainWindow.canJoin; commandId: "grid/sort/effect/selected"}
                CommandMenuItem { text: i18n.t("Text"); enabled: mainWindow.canJoin; commandId: "grid/sort/text/selected"}
                CommandMenuItem { text: i18n.t("Stripped Text"); enabled: mainWindow.canJoin; commandId: "grid/sort/text_stripped/selected"}
                CommandMenuItem { text: i18n.t("Layer"); enabled: mainWindow.canJoin; commandId: "grid/sort/layer/selected"}
            }
            CommandMenuItem { text: i18n.t("Swap Lines"); enabled: gridModel.selectionCount === 2; commandId: "grid/swap"}
            CommandMenuItem { objectName: "selectLinesMenuItem"; text: i18n.t("Select Lines..."); commandId: "tool/line/select"}
            CommandMenuItem { text: i18n.t("Select All"); commandId: "subtitle/select/all"; hotkeyContext: "Subtitle Grid"}
        }

        Menu {
            title: i18n.t("Timing")
            width: 290
            delegate: ShortcutMenuItem {}
            CommandMenuItem { text: i18n.t("Shift Times..."); commandId: "time/shift"}
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Snap Start to Video"); enabled: mainWindow.canSplit; commandId: "time/snap/start_video"}
            CommandMenuItem { text: i18n.t("Snap End to Video"); enabled: mainWindow.canSplit; commandId: "time/snap/end_video"}
            CommandMenuItem { text: i18n.t("Shift to Current Frame"); enabled: mainWindow.canSplit; commandId: "time/frame/current"}
            MenuSeparator {}
            Menu {
                title: i18n.t("Make Times Continuous")
                width: 210
                CommandMenuItem { text: i18n.t("Change Start"); enabled: mainWindow.canJoin; commandId: "time/continuous/start"}
                CommandMenuItem { text: i18n.t("Change End"); enabled: mainWindow.canJoin; commandId: "time/continuous/end"}
            }
            MenuSeparator {}
            CommandMenuItem { objectName: "addLeadInMenuItem"; text: i18n.t("Add lead in"); enabled: mainWindow.hasSelection; commandId: "time/lead/in"; hotkeyContext: "Audio"}
            CommandMenuItem { objectName: "addLeadOutMenuItem"; text: i18n.t("Add lead out"); enabled: mainWindow.hasSelection; commandId: "time/lead/out"; hotkeyContext: "Audio"}
        }

        Menu {
            title: i18n.t("Video")
            objectName: "videoMenu"
            width: 300
            delegate: ShortcutMenuItem {}
            CommandMenuItem { text: i18n.t("Open Video..."); commandId: "video/open"}
            CommandMenuItem { text: i18n.t("Close Video"); enabled: videoVM.hasVideo; commandId: "video/close"}
            RecentMenu { objectName: "recentVideosMenu"; paths: mruVM.videos; onOpenPath: path => videoVM.openLocalPath(path) }
            CommandMenuItem { text: i18n.t("Use Dummy Video..."); commandId: "video/open/dummy"}
            CommandMenuItem { text: i18n.t("Show Video Details"); enabled: videoVM.hasVideo; commandId: "video/details"}
            MenuSeparator {}
            Menu {
                objectName: "zoomMenu"
                title: i18n.t("Set Zoom")
                enabled: videoVM.hasVideo
                CommandMenuItem { text: i18n.t("Fit"); checkable: true; autoExclusive: true; checked: videoVM.zoomPercent === 0; commandId: "qt/video/zoom/fit"; hotkeyContext: "Video"}
                CommandMenuItem { text: "50%"; checkable: true; autoExclusive: true; checked: videoVM.zoomPercent === 50; commandId: "video/zoom/50"; hotkeyContext: "Video"}
                CommandMenuItem { text: "100%"; checkable: true; autoExclusive: true; checked: videoVM.zoomPercent === 100; commandId: "video/zoom/100"; hotkeyContext: "Video"}
                CommandMenuItem { text: "200%"; checkable: true; autoExclusive: true; checked: videoVM.zoomPercent === 200; commandId: "video/zoom/200"; hotkeyContext: "Video"}
            }
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Jump to..."); enabled: videoVM.hasVideo; commandId: "video/jump"}
            CommandMenuItem { text: i18n.t("Jump Video to Start"); enabled: mainWindow.canJump; commandId: "video/jump/start"}
            CommandMenuItem { text: i18n.t("Jump Video to End"); enabled: mainWindow.canJump; commandId: "video/jump/end"}
        }

        Menu {
            title: i18n.t("Audio")
            width: 300
            delegate: ShortcutMenuItem {}
            CommandMenuItem { text: i18n.t("Open Audio File..."); commandId: "audio/open"}
            CommandMenuItem {
                text: i18n.t("Open Audio from Video")
                enabled: videoVM.hasVideo && videoVM.filePath.length > 0
                commandId: "audio/open/video"
            }
            CommandMenuItem { text: i18n.t("Select Audio Track..."); enabled: audioVM.hasAudio; commandId: "qt/audio/select_track"}
            CommandMenuItem { text: i18n.t("Close Audio"); enabled: audioVM.hasAudio; commandId: "audio/close"}
            RecentMenu { objectName: "recentAudioMenu"; paths: mruVM.audio; onOpenPath: path => audioVM.openLocalPath(path) }
            CommandMenuItem { text: i18n.t("Use Dummy Audio"); commandId: "qt/audio/open/dummy"}
            MenuSeparator {}
            CommandMenuItem {
                text: i18n.t("Spectrum Display")
                checkable: true
                autoExclusive: true
                checked: audioVM.spectrumMode
                commandId: "audio/view/spectrum"
            }
            CommandMenuItem {
                text: i18n.t("Waveform Display")
                checkable: true
                autoExclusive: true
                checked: !audioVM.spectrumMode
                commandId: "audio/view/waveform"
            }
            MenuSeparator {}
            CommandMenuItem { text: i18n.t("Play Line"); enabled: audioVM.hasAudio; commandId: "audio/play/line"; hotkeyContext: "Audio"}
            CommandMenuItem { text: i18n.t("Stop"); enabled: audioVM.hasAudio && audioVM.isPlaying; commandId: "audio/stop"; hotkeyContext: "Audio"}
            CommandMenuItem {
                text: i18n.t("Follow Playhead")
                checkable: true
                checked: audioVM.followPlayhead
                commandId: "qt/audio/follow_playhead"
            }
        }

        Menu {
            id: automationMenu
            objectName: "automationMenu"
            title: i18n.t("Automation")
            width: 350
            delegate: ShortcutMenuItem {}
            onAboutToShow: automationVM.validateMenu()
            property AutomationMacroMenu macroEntries: AutomationMacroMenu {
                targetMenu: automationMenu
                submenuFactory: automationSubmenuFactory
                entries: automationVM.menu
                firstMacroIndex: 3
            }

            CommandMenuItem {
                objectName: "automationManagerMenuItem"
                text: i18n.t("Automation...")
                commandId: "am/manager"
            }
            CommandMenuItem {
                objectName: "automationReloadAutoloadMenuItem"
                text: i18n.t("Reload autoload Automation scripts")
                commandId: "am/reload/autoload"
            }
            MenuSeparator {
                objectName: "automationMacroSeparator"
                visible: automationVM.menu.length > 0
                height: visible ? implicitHeight : 0
            }
        }

        Menu {
            title: i18n.t("View")
            width: 240
            CommandMenuItem {
                objectName: "languageMenuItem"
                text: i18n.t("Language") + "..."
                commandId: "app/language"
            }
            CommandMenuItem {
                icon.source: "qrc:/icons/settings.svg"
                text: i18n.t("Options...")
                commandId: "app/options"
            }
            MenuSeparator {}
            ButtonGroup { id: workspaceGroup }
            CommandMenuItem { text: i18n.t("Subs Only View"); checkable: true; ButtonGroup.group: workspaceGroup; checked: mainWindow.workspaceLayout === 0; commandId: "app/display/subs"}
            CommandMenuItem { text: i18n.t("Video+Subs View"); checkable: true; ButtonGroup.group: workspaceGroup; checked: mainWindow.workspaceLayout === 1; commandId: "app/display/video_subs"}
            CommandMenuItem { text: i18n.t("Audio+Subs View"); checkable: true; ButtonGroup.group: workspaceGroup; checked: mainWindow.workspaceLayout === 2; commandId: "app/display/audio_subs"}
            CommandMenuItem { text: i18n.t("Full view"); checkable: true; ButtonGroup.group: workspaceGroup; checked: mainWindow.workspaceLayout === 3; commandId: "app/display/full"}
            MenuSeparator {}
            ButtonGroup { id: tagGroup }
            CommandMenuItem { text: i18n.t("Show Tags"); checkable: true; ButtonGroup.group: tagGroup; checked: gridModel.tagDisplayMode === 0; commandId: "grid/tags/show"}
            CommandMenuItem { text: i18n.t("Simplify Tags"); checkable: true; ButtonGroup.group: tagGroup; checked: gridModel.tagDisplayMode === 1; commandId: "grid/tags/simplify"}
            CommandMenuItem { text: i18n.t("Hide Tags"); checkable: true; ButtonGroup.group: tagGroup; checked: gridModel.tagDisplayMode === 2; commandId: "grid/tags/hide"}
            MenuSeparator {}
            CommandMenuItem {
                text: i18n.t("Dark Mode")
                checkable: true
                checked: themeVM.darkMode
                commandId: "qt/app/dark_mode"
            }
        }

        Menu {
            title: i18n.t("Help")
            CommandMenuItem {
                text: i18n.t("About")
                commandId: "app/about"
            }
            CommandMenuItem { text: i18n.t("Log Window"); commandId: "app/log"}
        }
    }

    // ---- Dialogs ---------------------------------------------------------

    LanguageDialog { id: languageDialog }
    AutomationManagerDialog { id: automationManagerDialog }
    LuaDialog { id: automationForm }
    AutomationProgressDialog {
        id: automationProgress
        visible: automationVM.progressVisible && !dismissed && !mainWindow.pendingCloseAfterAutomation
    }
    AutomationFileDialog { id: automationPicker }
    ExportSubtitlesDialog { id: exportSubtitlesDialog }
    Component {
        id: automationSubmenuFactory
        Menu {
            id: automationSubmenu
            property var entries: []
            width: 320
            delegate: ShortcutMenuItem {}
            property AutomationMacroMenu macroEntries: AutomationMacroMenu {
                targetMenu: automationSubmenu
                submenuFactory: automationSubmenuFactory
                entries: automationSubmenu.entries
            }
        }
    }

    Connections {
        target: automationVM
        function onOperationFinished(success) {
            if (mainWindow.pendingCloseAfterAutomation)
                Qt.callLater(mainWindow.resumeCloseAfterAutomation)
        }
        function onExecutionError(message) {
            automationErrorText.text = message
            automationErrorDialog.open()
        }
    }

    Dialog {
        id: automationErrorDialog
        objectName: "automationErrorDialog"
        title: i18n.t("Error")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(600, parent ? parent.width - 32 : 600)
        height: 300
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        contentItem: ScrollView {
            contentWidth: availableWidth
            TextArea {
                id: automationErrorText
                objectName: "automationErrorText"
                readOnly: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
            }
        }
    }

    Dialog {
        id: jumpDialog
        objectName: "jumpDialog"
        title: i18n.t("Jump to...")
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 320
        standardButtons: Dialog.Cancel
        property bool invalid: false
        function jump() {
            if (videoVM.jumpTo(jumpInput.text)) close()
            else invalid = true
        }
        onOpened: {
            invalid = false
            jumpInput.text = String(videoVM.currentFrame)
            jumpInput.forceActiveFocus()
            jumpInput.selectAll()
        }
        contentItem: ColumnLayout {
            TextField {
                id: jumpInput
                objectName: "jumpInput"
                Layout.fillWidth: true
                placeholderText: i18n.t("Frame or H:MM:SS.CC")
                onAccepted: jumpDialog.jump()
                onTextEdited: jumpDialog.invalid = false
            }
            Label { visible: jumpDialog.invalid; text: i18n.t("Invalid frame or timecode") }
            Button { Layout.alignment: Qt.AlignRight; text: i18n.t("Jump"); onClicked: jumpDialog.jump() }
        }
    }

    Dialog {
        id: detailsDialog
        objectName: "videoDetailsDialog"
        title: i18n.t("Video Details")
        modal: true
        anchors.centerIn: parent
        width: Math.min(540, mainWindow.width - 32)
        standardButtons: Dialog.Close
        contentItem: GridLayout {
            columns: 2
            Label { text: i18n.t("Path") }
            Label { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; text: videoVM.filePath || i18n.t("Dummy Video") }
            Label { text: i18n.t("Resolution") }
            Label { text: videoVM.videoWidth + " × " + videoVM.videoHeight }
            Label { text: i18n.t("FPS") }
            Label { text: Number(videoVM.fps).toFixed(3) }
            Label { text: i18n.t("Frames") }
            Label { text: videoVM.frameCount }
            Label { text: i18n.t("Keyframes") }
            Label { text: videoVM.keyframeCount }
        }
    }

    Dialog {
        id: linkedMediaDialog
        title: i18n.t("Load Linked Files?")
        modal: true
        anchors.centerIn: parent
        width: Math.min(540, mainWindow.width - 32)
        standardButtons: Dialog.Yes | Dialog.No
        property string videoPath: ""
        property string audioPath: ""
        property int zoomPercent: 0
        property int frame: -1
        property bool skipVideo: false
        property bool skipAudio: false
        onAccepted: mainWindow.openLinkedMedia(videoPath, audioPath, zoomPercent, frame, skipVideo, skipAudio)
        contentItem: Label {
            wrapMode: Text.WrapAnywhere
            text: [linkedMediaDialog.skipVideo ? "" : (linkedMediaDialog.videoPath || (videoVM.hasVideo ? i18n.t("Close Video") : "")),
                   linkedMediaDialog.skipAudio ? "" : (linkedMediaDialog.audioPath || (audioVM.hasAudio ? i18n.t("Close Audio") : ""))]
                  .filter(path => path.length > 0).join("\n")
        }
    }

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
        objectName: "unsavedDialog"
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
                    objectName: "unsavedCancelButton"
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
                objectName: "mediaSplit"
                visible: mainWindow.showVideoPanel || mainWindow.showAudioPanel
                SplitView.preferredHeight: 300
                SplitView.minimumHeight: 180
                orientation: Qt.Horizontal

                handle: Rectangle {
                    implicitWidth: 6
                    color: SplitHandle.hovered ? themeVM.outlineVariant : themeVM.surface
                }

                Loader {
                    id: videoPanelLoader
                    objectName: "videoPanelLoader"
                    source: "qrc:/VideoPanel.qml"
                    visible: mainWindow.showVideoPanel
                    SplitView.preferredWidth: 460
                    SplitView.minimumWidth: 320
                    SplitView.fillWidth: !mainWindow.showAudioPanel
                }

                Loader {
                    id: audioPanelLoader
                    objectName: "audioPanelLoader"
                    source: "qrc:/AudioPanel.qml"
                    visible: mainWindow.showAudioPanel
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
