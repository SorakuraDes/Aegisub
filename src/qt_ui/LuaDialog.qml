import QtQuick
import QtQuick.Controls.Material

Dialog {
    id: dialog
    objectName: "luaDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    title: request && request.title ? request.title : i18n.t("Automation")
    width: Math.max(0, Math.min(parent ? parent.width - 32 : 720,
                                Math.max(320, form.implicitWidth + leftPadding + rightPadding + 16,
                                         buttonStrip.implicitWidth + 24)))
    height: Math.max(0, Math.min(parent ? parent.height - 32 : 600, implicitHeight))
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    property var request: null
    property var pendingResult: false
    readonly property var buttons: request && request.buttons && request.buttons.length > 0
                                   ? request.buttons
                                   : [{text: i18n.t("OK"), role: "ok", result: ""},
                                      {text: i18n.t("Cancel"), role: "cancel", result: false}]
    readonly property int defaultIndex: {
        let selected = -1
        for (let i = 0; i < buttons.length; ++i) {
            if (["ok", "yes", "save"].indexOf(buttons[i].role) !== -1)
                selected = i
        }
        return selected
    }

    function start(newRequest) {
        if (!automationVM.busy || automationVM.cancelling)
            return
        request = newRequest
        pendingResult = false
        open()
    }

    function dismissRequest() {
        if (!request)
            return
        // Cancellation already released the worker; do not send a dialog reply.
        request = null
        close()
    }

    function resultFor(button) {
        return button.role === "cancel" ? false : button.result
    }

    function choose(index) {
        if (!request || index < 0 || index >= buttons.length)
            return
        pendingResult = resultFor(buttons[index])
        close()
    }

    onRejected: {
        for (let i = buttons.length - 1; i >= 0; --i) {
            if (buttons[i].role === "close" || buttons[i].role === "no") {
                pendingResult = resultFor(buttons[i])
                break
            }
        }
    }
    onClosed: {
        if (!request)
            return
        const values = form.values()
        const result = pendingResult
        request = null
        automationVM.completeDialog(result, values)
    }

    Connections {
        target: automationVM
        function onDialogRequested(request) { dialog.start(request) }
        function onOperationChanged() {
            if (automationVM.cancelling || !automationVM.busy)
                dialog.dismissRequest()
        }
    }

    onOpened: viewport.forceActiveFocus()

    contentItem: ScrollView {
        id: viewport
        Keys.onReturnPressed: event => {
            event.accepted = dialog.defaultIndex >= 0
            if (event.accepted) dialog.choose(dialog.defaultIndex)
        }
        Keys.onEnterPressed: event => {
            event.accepted = dialog.defaultIndex >= 0
            if (event.accepted) dialog.choose(dialog.defaultIndex)
        }
        clip: true
        implicitHeight: Math.max(24, form.implicitHeight)
        contentWidth: Math.max(availableWidth, form.implicitWidth)
        contentHeight: form.implicitHeight

        LuaForm {
            id: form
            width: viewport.contentWidth
            controls: dialog.request && dialog.request.controls ? dialog.request.controls : []
        }
    }

    footer: ScrollView {
        id: buttonViewport
        clip: true
        contentWidth: Math.max(availableWidth, buttonStrip.implicitWidth)
        contentHeight: buttonStrip.implicitHeight
        implicitHeight: buttonStrip.implicitHeight + 12
        leftPadding: 12
        rightPadding: 12
        bottomPadding: 12
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff

        Row {
            id: buttonStrip
            layoutDirection: Qt.RightToLeft
            width: buttonViewport.contentWidth
            spacing: 6
            Repeater {
                model: dialog.buttons
                Button {
                    required property int index
                    required property var modelData
                    objectName: "luaButton_" + index
                    text: modelData.text
                    highlighted: index === dialog.defaultIndex
                    onClicked: dialog.choose(index)
                }
            }
        }
    }
}
