import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: prefs
    objectName: "preferencesWindow"
    width: 670
    height: 435
    minimumWidth: 560
    minimumHeight: 325
    title: i18n.t("Preferences")
    color: themeVM.surface
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    readonly property int page: preferencesVM.preferencePage
    readonly property int hotkeysPageIndex: 9
    readonly property var pageNames: ["General", "Appearance", "Audio", "Video", "Interface", "Backup", "Advanced (FFMS2)", "Automation", "Colors", "Hotkeys"]
    readonly property var fields: [
        [["Save UI state in subtitles files", "App/Save UI State"], ["Undo Levels", "Limits/Undo Levels", 2, 10000],
         ["Recently used files", "Limits/MRU", 0, 16], ["Automatically load linked files", "App/Auto/Load Linked Files", ["Never", "Always", "Ask"], [0, 1, 2]]],
        [["Dark mode", "App/Dark Mode"]],
        [["Default timing length (ms)", "Timing/Default Duration", 0, 36000], ["Default lead-in length (ms)", "Audio/Lead/IN", 0, 36000], ["Default lead-out length (ms)", "Audio/Lead/OUT", 0, 36000], ["Lock scroll on cursor", "Audio/Lock Scroll on Cursor"],
         ["Snap markers by default", "Audio/Snap/Enable"], ["Maximum snap distance (px)", "Audio/Snap/Distance", 0, 25],
         ["Default mouse wheel to zoom", "Audio/Wheel Default to Zoom"]],
        [["Seek video to line start on selection change", "Video/Subtitle Sync"], ["Fast jump step in frames", "Video/Slider/Fast Jump Step", 1, 1000], ["Automatically open audio when opening video", "Video/Open Audio"],
         ["Automatic subtitle resolution", "Subtitle/Default Resolution/Auto"],
         ["Default subtitle width", "Subtitle/Default Resolution/Width", 1, 32768], ["Default subtitle height", "Subtitle/Default Resolution/Height", 1, 32768]],
        [["Enable syntax highlighting", "Subtitle/Highlight/Syntax"], ["Shift+Enter adds \\n", "Subtitle/Edit Box/Soft Line Break"],
         ["Edit box font face", "Subtitle/Edit Box/Font Face", "text"], ["Edit box font size", "Subtitle/Edit Box/Font Size", 3, 42],
         ["Subtitle grid font face", "Subtitle/Grid/Font Face", "text"], ["Subtitle grid font size", "Subtitle/Grid/Font Size", 3, 42], ["Replace override tags with", "Subtitle/Grid/Hide Overrides Char", "text"],
         ["Maximum characters per line", "Subtitle/Character Limit", 0, 1000], ["CPS warning threshold", "Subtitle/Character Counter/CPS Warning Threshold", 0, 1000],
         ["CPS error threshold", "Subtitle/Character Counter/CPS Error Threshold", 0, 1000], ["Ignore whitespace", "Subtitle/Character Counter/Ignore Whitespace"], ["Ignore punctuation", "Subtitle/Character Counter/Ignore Punctuation"]],
        [["Automatic Save", "App/Auto/Save"], ["Interval in seconds", "App/Auto/Save Every Seconds", 1, 2147483],
         ["Autosave path", "Path/Auto/Save", "text"], ["Autosave after every change", "App/Auto/Save on Every Change"],
         ["Automatic Backup", "App/Auto/Backup"], ["Backup path", "Path/Auto/Backup", "text"]],
        [["Debug log verbosity", "Provider/FFmpegSource/Log Level", ["Quiet", "Panic", "Fatal", "Error", "Warning", "Info", "Verbose", "Debug", "Trace"],
          ["quiet", "panic", "fatal", "error", "warning", "info", "verbose", "debug", "trace"]],
         ["Decoding threads", "Provider/Video/FFmpegSource/Decoding Threads", -1, 256], ["Enable unsafe seeking", "Provider/Video/FFmpegSource/Unsafe Seeking"]],
        [["Include path", "Path/Automation/Include", "text"], ["Auto-load path", "Path/Automation/Autoload", "text"],
         ["Trace level", "Automation/Trace Level", ["0: Fatal", "1: Error", "2: Warning", "3: Hint", "4: Debug", "5: Trace"], [0, 1, 2, 3, 4, 5]]],
        [], []
    ]

    readonly property var hotkeysItem: hotkeysLoader.item
    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        enabled: !restoreDialog.visible && (!hotkeysItem || !hotkeysItem.recording)
        onActivated: prefs.close()
    }
    onClosing: {
        restoreDialog.close()
        if (hotkeysItem) hotkeysItem.cancelRecording()
    }
    onActiveChanged: if (!active && hotkeysItem) hotkeysItem.cancelRecording()

    Dialog {
        id: restoreDialog
        objectName: "hotkeyRestoreDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(380, prefs.width - 32)
        modal: true
        title: i18n.t("Restore default shortcuts?")
        contentItem: Label {
            text: i18n.t("Custom shortcuts for commands available in Qt will be replaced.")
            color: themeVM.onSurface
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            ToolDialogButton {
                objectName: "hotkeyCancelRestoreDefaults"
                text: i18n.t("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            ToolDialogButton {
                objectName: "hotkeyConfirmRestoreDefaults"
                text: i18n.t("Restore Defaults")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: if (prefs.hotkeysItem) prefs.hotkeysItem.restoreDefaults()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            ListView {
                id: nav
                objectName: "preferencesNavigation"
                Layout.fillHeight: true
                Layout.preferredWidth: 152
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                model: prefs.pageNames
                currentIndex: prefs.page
                activeFocusOnTab: true
                keyNavigationEnabled: false
                Keys.onUpPressed: preferencesVM.preferencePage = Math.max(0, prefs.page - 1)
                Keys.onDownPressed: preferencesVM.preferencePage = Math.min(count - 1, prefs.page + 1)
                delegate: ItemDelegate {
                    required property int index
                    required property string modelData
                    objectName: index === prefs.hotkeysPageIndex ? "preferencesHotkeysNav" : ""
                    width: nav.width
                    height: 37
                    text: i18n.t(modelData)
                    highlighted: index === prefs.page
                    onClicked: preferencesVM.preferencePage = index
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label { text: i18n.t(prefs.pageNames[prefs.page]); color: themeVM.onSurface; font.pixelSize: 19; font.bold: true }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: themeVM.outlineVariant }
                ScrollView {
                    id: formScroll
                    visible: prefs.page !== prefs.hotkeysPageIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    Connections {
                        target: prefs
                        function onPageChanged() { formScroll.contentItem.contentY = 0 }
                    }
                    ColumnLayout {
                        width: formScroll.availableWidth
                        RowLayout {
                            Layout.fillWidth: true
                            visible: prefs.page === 0
                            Label {
                                Layout.fillWidth: true
                                text: i18n.t("Language")
                                color: themeVM.onSurface
                            }
                            ComboBox {
                                id: languagePicker
                                objectName: "preferencesLanguagePicker"
                                Layout.preferredWidth: 200
                                Accessible.name: i18n.t("Language")
                                enabled: !i18n.forced
                                model: [{ code: "", name: i18n.t("System default") }].concat(i18n.availableLanguages)
                                textRole: "name"
                                valueRole: "code"
                                function configuredIndex() {
                                    return Math.max(0, indexOfValue(i18n.forced ? i18n.language : i18n.preference))
                                }
                                currentIndex: configuredIndex()
                                onActivated: {
                                    if (!i18n.setLanguage(currentValue))
                                        currentIndex = Qt.binding(configuredIndex)
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: prefs.page === 0 && i18n.error.length > 0
                            text: i18n.error
                            textFormat: Text.PlainText
                            color: themeVM.danger
                            wrapMode: Text.WrapAnywhere
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: prefs.page === 7
                            text: i18n.t("Path separator") + ": |"
                            color: themeVM.onSurfaceVariant
                        }
                        Repeater {
                            model: prefs.fields[prefs.page]
                            delegate: RowLayout {
                                id: row
                                required property var modelData
                                readonly property string key: modelData[1]
                                readonly property var value: preferencesVM.optionValues[key]
                                readonly property string label: i18n.t(modelData[0])
                                Layout.fillWidth: true
                                spacing: 12
                                Label { Layout.fillWidth: true; text: row.label; wrapMode: Text.Wrap; color: themeVM.onSurface }
                                Loader {
                                    sourceComponent: row.modelData.length === 2 ? toggle : row.modelData[2] === "text" ? edit :
                                                     Array.isArray(row.modelData[2]) ? choice : number
                                }
                                Component {
                                    id: toggle
                                    Switch {
                                        objectName: row.key
                                        Accessible.name: row.label
                                        checked: !!row.value
                                        onToggled: preferencesVM.setOption(row.key, checked)
                                    }
                                }
                                Component {
                                    id: number
                                    SpinBox {
                                        objectName: row.key
                                        Accessible.name: row.label
                                        width: 155
                                        editable: true
                                        from: row.modelData[2]; to: row.modelData[3]
                                        value: Number(row.value)
                                        onValueModified: preferencesVM.setOption(row.key, value)
                                    }
                                }
                                Component {
                                    id: choice
                                    ComboBox {
                                        objectName: row.key
                                        Accessible.name: row.label
                                        width: 155
                                        model: row.modelData[2].map(label => i18n.t(label))
                                        currentIndex: row.modelData[3].indexOf(typeof row.modelData[3][0] === "number" ? Number(row.value) : row.value)
                                        onActivated: index => preferencesVM.setOption(row.key, row.modelData[3][index])
                                    }
                                }
                                Component {
                                    id: edit
                                    TextField {
                                        objectName: row.key
                                        Accessible.name: row.label
                                        width: 165
                                        text: row.value || ""
                                        selectByMouse: true
                                        onEditingFinished: preferencesVM.setOption(row.key, text)
                                    }
                                }
                            }
                        }
                        Loader {
                            Layout.fillWidth: true
                            active: prefs.page === 1
                            sourceComponent: accents
                        }
                        Loader {
                            Layout.fillWidth: true
                            active: prefs.page === 8
                            sourceComponent: colors
                        }
                    }
                }
                Loader {
                    id: hotkeysLoader
                    objectName: "preferencesHotkeysLoader"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: prefs.page === prefs.hotkeysPageIndex
                    visible: active
                    sourceComponent: hotkeysPage
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: themeVM.outlineVariant }
        RowLayout {
            Layout.fillWidth: true
            visible: preferencesVM.persistenceError.length !== 0
            Label {
                objectName: "preferencesPersistenceError"
                Layout.fillWidth: true
                text: i18n.t("Could not save preferences") + ": " + preferencesVM.persistenceError
                color: themeVM.danger
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
                Accessible.role: Accessible.AlertMessage
            }
            ToolDialogButton {
                objectName: "preferencesRetrySave"
                text: i18n.t("Retry Save")
                onClicked: preferencesVM.retrySave()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "config.json: " + preferencesVM.configFilePath
                font.pixelSize: 11
                color: themeVM.onSurfaceVariant
                elide: Text.ElideMiddle
            }
            ToolDialogButton { text: i18n.t("Close"); onClicked: prefs.close() }
        }
    }
    Component {
        id: hotkeysPage
        ColumnLayout {
            id: hotkeys
            objectName: "preferencesHotkeysPage"
            spacing: 6

            property var recordingTarget: null
            property string actionError: ""
            property string selectedContext: ""
            readonly property bool recording: recordingTarget !== null
            readonly property var filteredRows: {
                hotkeyVM.revision
                return hotkeyVM.rows(selectedContext, searchField.text)
            }

            function startRecording(context, command, oldKey) {
                actionError = ""
                recordingTarget = { context: context, command: command, oldKey: oldKey }
                if (hotkeyVM.beginRecording(prefs) === false) {
                    recordingTarget = null
                    actionError = hotkeyVM.error || i18n.t("Could not start shortcut recording")
                }
            }

            function cancelRecording() {
                if (!recording) return
                recordingTarget = null
                hotkeyVM.cancelRecording()
            }

            function changeBinding(context, command, oldKey, newKey) {
                actionError = hotkeyVM.rebind(context, command, oldKey, newKey) === false
                    ? (hotkeyVM.error || i18n.t("Could not change shortcut")) : ""
            }

            function restoreDefaults() {
                actionError = hotkeyVM.restoreDefaults() === false
                    ? (hotkeyVM.error || i18n.t("Could not restore default shortcuts")) : ""
            }

            Component.onDestruction: cancelRecording()
            Connections {
                target: hotkeyVM
                function onRecorded(key) {
                    const target = hotkeys.recordingTarget
                    hotkeys.recordingTarget = null
                    if (target && key.length > 0)
                        hotkeys.changeBinding(target.context, target.command, target.oldKey, key)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                enabled: !hotkeys.recording
                ComboBox {
                    id: contextPicker
                    objectName: "hotkeyContext"
                    Layout.preferredWidth: 146
                    Accessible.name: i18n.t("Context")
                    textRole: "label"
                    valueRole: "context"
                    currentIndex: Math.max(0, indexOfValue(hotkeys.selectedContext))
                    onActivated: hotkeys.selectedContext = currentValue || ""
                    model: {
                        hotkeyVM.revision
                        return [{ label: i18n.t("All contexts"), context: "" }].concat(
                            Array.from(hotkeyVM.contexts, name => ({ label: i18n.t(name), context: name })))
                    }
                }
                TextField {
                    id: searchField
                    objectName: "hotkeySearch"
                    Layout.fillWidth: true
                    placeholderText: i18n.t("Search shortcuts")
                    Accessible.name: placeholderText
                    selectByMouse: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: hotkeys.recording
                Label {
                    objectName: "hotkeyRecordingPrompt"
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    text: i18n.t("Press a key combination")
                    color: themeVM.primary
                    font.bold: true
                    wrapMode: Text.Wrap
                }
                ToolDialogButton {
                    objectName: "hotkeyCancelRecording"
                    text: i18n.t("Cancel")
                    onClicked: hotkeys.cancelRecording()
                }
            }

            ListView {
                id: hotkeyList
                objectName: "hotkeyList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                enabled: !hotkeys.recording
                model: hotkeys.filteredRows
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ColumnLayout {
                    id: commandRow
                    required property var modelData
                    readonly property string suffix: "/" + modelData.context + "/" + modelData.command
                    objectName: "hotkeyRow" + suffix
                    width: Math.max(0, hotkeyList.width - 14)
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: commandRow.modelData.command
                                textFormat: Text.PlainText
                                color: themeVM.onSurface
                                wrapMode: Text.WrapAnywhere
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: i18n.t(commandRow.modelData.context)
                                    textFormat: Text.PlainText
                                    color: themeVM.onSurfaceVariant
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    objectName: "hotkeyConflict" + commandRow.suffix
                                    visible: commandRow.modelData.conflict
                                    text: i18n.t("Conflict")
                                    color: themeVM.danger
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                            Label {
                                visible: !commandRow.modelData.managed
                                text: i18n.t("Not available in Qt")
                                color: themeVM.onSurfaceVariant
                                font.pixelSize: 11
                            }
                        }
                        ColumnLayout {
                            Layout.preferredWidth: Math.min(184, commandRow.width * 0.45)
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                visible: commandRow.modelData.bindings.length === 0
                                text: i18n.t("Unassigned")
                                color: themeVM.onSurfaceVariant
                                horizontalAlignment: Text.AlignRight
                            }
                            Repeater {
                                model: commandRow.modelData.bindings
                                delegate: RowLayout {
                                    id: bindingRow
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        Layout.fillWidth: true
                                        visible: !commandRow.modelData.managed
                                        text: bindingRow.modelData
                                        textFormat: Text.PlainText
                                        color: themeVM.onSurfaceVariant
                                        wrapMode: Text.WrapAnywhere
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Button {
                                        id: editBinding
                                        objectName: "hotkeyEdit" + commandRow.suffix + "/" + bindingRow.index
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        visible: commandRow.modelData.managed
                                        text: bindingRow.modelData
                                        Accessible.name: i18n.t("Change shortcut") + ": " + text + " (" + commandRow.modelData.command + ")"
                                        ToolTip.visible: hovered || visualFocus
                                        ToolTip.text: Accessible.name
                                        ToolTip.delay: 400
                                        contentItem: Label {
                                            text: editBinding.text
                                            textFormat: Text.PlainText
                                            elide: Text.ElideMiddle
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        onClicked: hotkeys.startRecording(commandRow.modelData.context, commandRow.modelData.command, bindingRow.modelData)
                                    }
                                    ToolButton {
                                        objectName: "hotkeyRemove" + commandRow.suffix + "/" + bindingRow.index
                                        visible: commandRow.modelData.managed
                                        Accessible.name: i18n.t("Remove shortcut") + ": " + bindingRow.modelData + " (" + commandRow.modelData.command + ")"
                                        icon.source: "qrc:/icons/close.svg"
                                        icon.width: 16; icon.height: 16
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 32
                                        ToolTip.visible: hovered || visualFocus
                                        ToolTip.text: Accessible.name
                                        ToolTip.delay: 400
                                        onClicked: hotkeys.changeBinding(commandRow.modelData.context, commandRow.modelData.command, bindingRow.modelData, "")
                                    }
                                }
                            }
                        }
                        ToolButton {
                            objectName: "hotkeyAdd" + commandRow.suffix
                            enabled: commandRow.modelData.managed
                            opacity: enabled ? 1 : 0
                            Accessible.name: i18n.t("Add shortcut") + ": " + commandRow.modelData.command
                            icon.source: "qrc:/icons/add.svg"
                            icon.width: 16; icon.height: 16
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 32
                            ToolTip.visible: hovered || visualFocus
                            ToolTip.text: Accessible.name
                            ToolTip.delay: 400
                            onClicked: hotkeys.startRecording(commandRow.modelData.context, commandRow.modelData.command, "")
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: themeVM.outlineVariant }
                }
                Label {
                    anchors.centerIn: parent
                    width: Math.max(0, hotkeyList.width - 16)
                    visible: hotkeyList.count === 0
                    text: i18n.t("No matching shortcuts")
                    color: themeVM.onSurfaceVariant
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            Label {
                objectName: "hotkeyError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: hotkeys.actionError
                textFormat: Text.PlainText
                color: themeVM.danger
                wrapMode: Text.WrapAnywhere
                Accessible.role: Accessible.AlertMessage
            }
            RowLayout {
                Layout.fillWidth: true
                Switch {
                    objectName: "hotkeyAlwaysEnabled"
                    Layout.fillWidth: true
                    enabled: !hotkeys.recording
                    text: i18n.t("Enable Medusa timing hotkeys")
                    checked: !!preferencesVM.optionValues["Audio/Medusa Timing Hotkeys"]
                    onToggled: {
                        if (!preferencesVM.setOption("Audio/Medusa Timing Hotkeys", checked))
                            checked = Qt.binding(() => !!preferencesVM.optionValues["Audio/Medusa Timing Hotkeys"])
                    }
                }
                ToolDialogButton {
                    objectName: "hotkeyRestoreDefaults"
                    enabled: !hotkeys.recording
                    text: i18n.t("Restore Defaults")
                    onClicked: restoreDialog.open()
                }
            }
        }
    }
    Component {
        id: colors
        ColumnLayout {
            Repeater {
                model: [
                    ["audio", "Audio Display", "Colour/Audio Display/", ["Play Cursor", "Line boundary Start", "Line boundary End", "Keyframe"]],
                    ["syntax", "Syntax Highlighting", "Colour/Subtitle/Syntax/", ["Normal", "Brackets", "Tags", "Parameters", "Error", "Line Break"]],
                    ["grid", "Subtitle Grid", "Colour/Subtitle Grid/", ["Standard", "Selection", "Active Border", "CPS Error", "Background/Background", "Background/Selection", "Background/Comment", "Background/Open Fold", "Background/Closed Fold"]],
                    ["visual", "Visual Tools", "Colour/Visual Tools/", ["Lines Primary", "Highlight Primary", "Shaded Area Alpha"]]
                ]
                delegate: ColumnLayout {
                    id: group
                    required property var modelData
                    readonly property string groupId: modelData[0]
                    readonly property string prefix: modelData[2]
                    readonly property bool custom: {
                        preferencesVM.colorRevision
                        return preferencesVM.colorOverridesEnabled(groupId)
                    }
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: i18n.t(group.modelData[1])
                            color: themeVM.onSurface
                            wrapMode: Text.Wrap
                            font.bold: true
                        }
                        Switch {
                            objectName: "colorOverrides/" + group.groupId
                            text: group.custom ? i18n.t("Custom override") : i18n.t("Follow Monet theme")
                            Accessible.name: i18n.t(group.modelData[1]) + ": " + text
                            checked: group.custom
                            onToggled: preferencesVM.setColorOverridesEnabled(group.groupId, checked)
                        }
                    }
                    Repeater {
                        model: group.custom ? group.modelData[3] : []
                        delegate: RowLayout {
                            id: colorRow
                            required property string modelData
                            readonly property string key: group.prefix + modelData
                            readonly property bool alpha: modelData === "Shaded Area Alpha"
                            readonly property var value: preferencesVM.optionValues[key]
                            readonly property string label: i18n.t(modelData.startsWith("Background/") ? modelData.substring(11) + " background" : modelData)
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: colorRow.label; color: themeVM.onSurface; wrapMode: Text.Wrap }
                            Rectangle {
                                visible: !colorRow.alpha
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 4
                                color: colorRow.alpha ? "transparent" : colorRow.value
                                border.color: themeVM.outline
                            }
                            Loader {
                                sourceComponent: colorRow.alpha ? alphaSpin : rgbInput
                            }
                            Component {
                                id: rgbInput
                                TextField {
                                    objectName: colorRow.key
                                    Accessible.name: colorRow.label
                                    width: 110
                                    text: String(colorRow.value).toUpperCase()
                                    selectByMouse: true
                                    maximumLength: 7
                                    validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                                    onEditingFinished: {
                                        if (acceptableInput)
                                            preferencesVM.setOption(colorRow.key, text)
                                        text = Qt.binding(() => String(colorRow.value).toUpperCase())
                                    }
                                    onActiveFocusChanged: if (!activeFocus && !acceptableInput) text = Qt.binding(() => String(colorRow.value).toUpperCase())
                                }
                            }
                            Component {
                                id: alphaSpin
                                SpinBox {
                                    objectName: colorRow.key
                                    Accessible.name: colorRow.label
                                    width: 110
                                    from: 0; to: 10; stepSize: 1
                                    value: Math.round(10 * Number(colorRow.value))
                                    textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, "f", 1)
                                    onValueModified: preferencesVM.setOption(colorRow.key, value / 10)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: accents
        ColumnLayout {
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: i18n.t("Accent color"); color: themeVM.onSurface }
                Repeater {
                    model: themeVM.presetColors
                    delegate: ToolButton {
                        id: swatch
                        required property var modelData
                        readonly property bool selected: Qt.colorEqual(themeVM.seedColor, modelData.color)
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 36
                        padding: 0
                        hoverEnabled: true
                        Accessible.name: i18n.t(modelData.name)
                        Accessible.checkable: true
                        Accessible.checked: selected
                        ToolTip.visible: hovered || visualFocus
                        ToolTip.text: i18n.t(modelData.name)
                        ToolTip.delay: 400
                        onClicked: themeVM.seedColor = modelData.color
                        contentItem: Item {
                            Rectangle {
                                anchors.centerIn: parent
                                width: 26; height: 26; radius: 13
                                color: themeVM.surface
                                border.width: swatch.visualFocus ? 3 : swatch.selected ? 2 : 1
                                border.color: swatch.visualFocus || swatch.selected ? themeVM.onSurface : themeVM.outlineVariant
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    radius: width / 2
                                    color: swatch.modelData.color
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: i18n.t("Custom color"); color: themeVM.onSurface }
                TextField {
                    Layout.preferredWidth: 120
                    Accessible.name: i18n.t("Custom color")
                    text: themeVM.seedColor.toString().toUpperCase()
                    selectByMouse: true
                    maximumLength: 7
                    validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                    color: acceptableInput ? themeVM.onSurface : themeVM.danger
                    onTextEdited: if (acceptableInput && !Qt.colorEqual(themeVM.seedColor, text)) themeVM.seedColor = text
                    onActiveFocusChanged: if (!activeFocus && !acceptableInput) text = Qt.binding(() => themeVM.seedColor.toString().toUpperCase())
                }
            }
        }
    }
}
