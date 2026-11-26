import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: progress
    objectName: "automationProgressDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    visible: automationVM.progressVisible && !dismissed
    closePolicy: Popup.NoAutoClose
    width: Math.max(0, Math.min(parent ? parent.width - 32 : 640, logView.visible ? 640 : 460))
    height: Math.max(0, Math.min(parent ? parent.height - 32 : 425, 160 + (logView.visible ? 225 : 0)))
    title: automationVM.progressTitle || i18n.t("Automation")
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    property bool dismissed: false
    readonly property bool vmVisible: automationVM.progressVisible
    onVmVisibleChanged: if (!vmVisible) dismissed = false

    function cancelOrDismiss() {
        if (!automationVM.busy)
            dismissed = true
        else if (!automationVM.cancelling)
            automationVM.cancelOperation()
    }

    contentItem: ColumnLayout {
        spacing: 8
        Keys.onEscapePressed: progress.cancelOrDismiss()

        Label {
            objectName: "automationProgressTask"
            Layout.fillWidth: true
            visible: text.length > 0
            text: automationVM.progressTask
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }

        ProgressBar {
            objectName: "automationProgressBar"
            Layout.fillWidth: true
            Accessible.name: progress.title
            from: 0
            to: 100
            value: automationVM.progressValue
        }

        ScrollView {
            id: logView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 225
            visible: logOutput.text.length > 0
            contentWidth: availableWidth
            clip: true
            TextArea {
                id: logOutput
                objectName: "automationProgressLog"
                text: automationVM.progressLog
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.family: "monospace"
                onTextChanged: cursorPosition = length
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "automationProgressCancelButton"
            text: !automationVM.busy ? i18n.t("Close")
                  : automationVM.cancelling ? i18n.t("Cancelling...") : i18n.t("Cancel")
            enabled: !automationVM.busy || !automationVM.cancelling
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: progress.cancelOrDismiss()
        }
    }
}
