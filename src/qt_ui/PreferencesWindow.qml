import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: prefs
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
    readonly property var pageNames: ["General", "Appearance", "Audio", "Video", "Interface", "Backup", "Advanced (FFMS2)"]
    readonly property var fields: [
        [["Save UI state in subtitles files", "App/Save UI State"], ["Undo Levels", "Limits/Undo Levels", 2, 10000],
         ["Recently used files", "Limits/MRU", 0, 16], ["Automatically load linked files", "App/Auto/Load Linked Files", ["Never", "Always", "Ask"], [0, 1, 2]]],
        [["Dark mode", "App/Dark Mode"]],
        [["Default timing length (ms)", "Timing/Default Duration", 0, 36000], ["Lock scroll on cursor", "Audio/Lock Scroll on Cursor"],
         ["Snap markers by default", "Audio/Snap/Enable"], ["Maximum snap distance (px)", "Audio/Snap/Distance", 0, 25],
         ["Default mouse wheel to zoom", "Audio/Wheel Default to Zoom"]],
        [["Seek video to line start on selection change", "Video/Subtitle Sync"], ["Automatically open audio when opening video", "Video/Open Audio"],
         ["Automatic subtitle resolution", "Subtitle/Default Resolution/Auto"],
         ["Default subtitle width", "Subtitle/Default Resolution/Width", 1, 32768], ["Default subtitle height", "Subtitle/Default Resolution/Height", 1, 32768]],
        [["Enable syntax highlighting", "Subtitle/Highlight/Syntax"], ["Edit box font size", "Subtitle/Edit Box/Font Size", 3, 42],
         ["Subtitle grid font size", "Subtitle/Grid/Font Size", 3, 42], ["Replace override tags with", "Subtitle/Grid/Hide Overrides Char", "text"],
         ["Maximum characters per line", "Subtitle/Character Limit", 0, 1000]],
        [["Automatic Save", "App/Auto/Save"], ["Interval in seconds", "App/Auto/Save Every Seconds", 1, 2147483],
         ["Autosave path", "Path/Auto/Save", "text"], ["Autosave after every change", "App/Auto/Save on Every Change"],
         ["Automatic Backup", "App/Auto/Backup"], ["Backup path", "Path/Auto/Backup", "text"]],
        [["Debug log verbosity", "Provider/FFmpegSource/Log Level", ["Quiet", "Panic", "Fatal", "Error", "Warning", "Info", "Verbose", "Debug", "Trace"],
          ["quiet", "panic", "fatal", "error", "warning", "info", "verbose", "debug", "trace"]],
         ["Decoding threads", "Provider/Video/FFmpegSource/Decoding Threads", -1, 256], ["Enable unsafe seeking", "Provider/Video/FFmpegSource/Unsafe Seeking"]]
    ]

    Shortcut { sequence: StandardKey.Cancel; context: Qt.WindowShortcut; onActivated: prefs.close() }

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
                model: prefs.pageNames
                currentIndex: prefs.page
                activeFocusOnTab: true
                keyNavigationEnabled: false
                Keys.onUpPressed: preferencesVM.preferencePage = Math.max(0, prefs.page - 1)
                Keys.onDownPressed: preferencesVM.preferencePage = Math.min(count - 1, prefs.page + 1)
                delegate: ItemDelegate {
                    required property int index
                    required property string modelData
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
                                        currentIndex: row.modelData[3].indexOf(row.value)
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
                    }
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
            Button {
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
            Button { text: i18n.t("Close"); onClicked: prefs.close() }
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
