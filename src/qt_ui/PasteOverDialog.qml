import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: root
    objectName: "pasteOverDialog"
    title: i18n.t("Select Fields to Paste Over")
    modality: Qt.ApplicationModal
    color: themeVM.surface
    readonly property int preferredWidth: Math.max(360, form.implicitWidth + 32)
    readonly property int preferredHeight: form.implicitHeight + 32
    width: preferredWidth
    height: preferredHeight
    minimumWidth: nativeWindowRaise ? preferredWidth : 0
    minimumHeight: nativeWindowRaise ? preferredHeight : 0

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    function open() {
        if (!visible && !pasteOverVM.prepare())
            return
        show()
        if (nativeWindowRaise) raise()
        requestActivate()
        if (okButton.enabled)
            okButton.forceActiveFocus()
    }

    function confirm() {
        if (pasteOverVM.canAccept && pasteOverVM.accept())
            close()
    }

    onVisibleChanged: if (!visible) pasteOverVM.cancel()

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: root.close()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.WindowShortcut
        enabled: root.visible && pasteOverVM.canAccept
        onActivated: root.confirm()
    }

    ColumnLayout {
        id: form
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: i18n.t("Fields")
            color: themeVM.onSurfaceVariant
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            enabled: pasteOverVM.canAccept

            Repeater {
                model: [
                    { key: "Comment", label: i18n.t("Comment") },
                    { key: "Layer", label: i18n.t("Layer") },
                    { key: "Start", label: i18n.t("Start Time") },
                    { key: "End", label: i18n.t("End Time") },
                    { key: "Style", label: i18n.t("Style") },
                    { key: "Actor", label: i18n.t("Actor") },
                    { key: "MarginL", label: i18n.t("Margin Left") },
                    { key: "MarginR", label: i18n.t("Margin Right") },
                    { key: "MarginV", label: i18n.t("Margin Vertical") },
                    { key: "Effect", label: i18n.t("Effect") },
                    { key: "Text", label: i18n.t("Text") }
                ]

                CheckBox {
                    required property int index
                    required property var modelData
                    objectName: "pasteOverField" + modelData.key
                    Layout.fillWidth: true
                    implicitHeight: Math.max(themeVM.controlHeight, implicitContentHeight + 8)
                    topPadding: 0
                    bottomPadding: 0
                    text: modelData.label
                    checked: pasteOverVM.fields[index]
                    onToggled: pasteOverVM.setField(index, checked)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            enabled: pasteOverVM.canAccept

            Button {
                objectName: "pasteOverAll"
                Layout.fillWidth: true
                text: i18n.t("&All")
                onClicked: pasteOverVM.selectAll()
            }
            Button {
                objectName: "pasteOverNone"
                Layout.fillWidth: true
                text: i18n.t("&None")
                onClicked: pasteOverVM.selectNone()
            }
            Button {
                objectName: "pasteOverTimes"
                Layout.fillWidth: true
                text: i18n.t("&Times")
                onClicked: pasteOverVM.selectTimes()
            }
            Button {
                objectName: "pasteOverText"
                Layout.fillWidth: true
                text: i18n.t("T&ext")
                onClicked: pasteOverVM.selectText()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.visible && !pasteOverVM.canAccept
            wrapMode: Text.WordWrap
            text: i18n.t("The document or selection has changed.")
            color: themeVM.onSurface
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: "pasteOverCancel"
                text: i18n.t("Cancel")
                onClicked: root.close()
            }
            ToolDialogButton {
                id: okButton
                objectName: "pasteOverOk"
                text: i18n.t("OK")
                primary: true
                enabled: pasteOverVM.canAccept
                onClicked: root.confirm()
            }
        }
    }
}
