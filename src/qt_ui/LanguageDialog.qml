import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "languageDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 24 : 440)
    modal: true
    title: i18n.t("Language")
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface
    closePolicy: Popup.CloseOnEscape

    readonly property var choices: {
        const entries = [{ code: "", name: i18n.t("System default") }]
        const available = i18n.availableLanguages
        for (let i = 0; i < available.length; ++i)
            entries.push(available[i])
        return entries
    }

    onAboutToShow: {
        i18n.clearError()
        const code = i18n.forced ? i18n.language : i18n.preference
        picker.currentIndex = Math.max(0, choices.findIndex(choice => choice.code === code))
    }
    onOpened: {
        if (picker.enabled)
            picker.forceActiveFocus()
        else
            cancelButton.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 12
        ComboBox {
            id: picker
            objectName: "languagePicker"
            Layout.fillWidth: true
            enabled: !i18n.forced
            Accessible.name: i18n.t("Select Aegisub interface language")
            model: dialog.choices
            textRole: "name"
            valueRole: "code"
        }
        Label {
            Layout.fillWidth: true
            visible: i18n.forced
            text: i18n.t("Language is set by AEGISUB_QT_LANGUAGE.")
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "languagePersistenceError"
            Layout.fillWidth: true
            visible: i18n.error.length > 0
            text: i18n.error
            textFormat: Text.PlainText
            color: themeVM.danger
            wrapMode: Text.WrapAnywhere
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button {
                id: cancelButton
                text: i18n.forced ? i18n.t("Close") : i18n.t("Cancel")
                onClicked: dialog.reject()
            }
            Button {
                objectName: "languageApplyButton"
                visible: !i18n.forced
                enabled: picker.currentIndex >= 0
                text: i18n.t("OK")
                onClicked: {
                    if (i18n.setLanguage(picker.currentValue))
                        dialog.accept()
                }
            }
        }
    }
}
