import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: root
    objectName: "resampleDialog"
    title: i18n.t("Resample Resolution")
    modality: Qt.ApplicationModal
    color: themeVM.surface
    width: Math.max(380, form.implicitWidth + 32)
    height: form.implicitHeight + 32
    minimumWidth: Math.max(380, form.implicitWidth + 32)
    minimumHeight: form.implicitHeight + 32

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    function open() {
        resampleVM.refreshSource()
        targetWidth.text = String(resampleVM.sourceWidth)
        targetHeight.text = String(resampleVM.sourceHeight)
        errorLabel.visible = false
        show()
        raise()
        requestActivate()
        targetWidth.forceActiveFocus()
        targetWidth.selectAll()
    }

    function confirm() {
        if (!applyButton.enabled)
            return
        if (resampleVM.apply(targetWidth.text, targetHeight.text))
            close()
        else
            errorLabel.visible = true
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: root.close()
    }

    ColumnLayout {
        id: form
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: i18n.t("Script Resolution")
                color: themeVM.onSurfaceVariant
            }
            Item { Layout.fillWidth: true }
            Label {
                text: resampleVM.sourceWidth + " \u00d7 " + resampleVM.sourceHeight
                color: themeVM.onSurface
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 6

            Label { text: i18n.t("Destination Width"); color: themeVM.onSurface }
            TextField {
                id: targetWidth
                objectName: "resampleWidth"
                Accessible.name: i18n.t("Destination Width")
                Layout.fillWidth: true
                implicitHeight: themeVM.controlHeight
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 2147483647; locale: "C" }
                onAccepted: root.confirm()
                onTextChanged: errorLabel.visible = false
            }
            Label { text: i18n.t("Destination Height"); color: themeVM.onSurface }
            TextField {
                id: targetHeight
                objectName: "resampleHeight"
                Accessible.name: i18n.t("Destination Height")
                Layout.fillWidth: true
                implicitHeight: themeVM.controlHeight
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 2147483647; locale: "C" }
                onAccepted: root.confirm()
                onTextChanged: errorLabel.visible = false
            }
        }

        Label {
            id: errorLabel
            visible: false
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: i18n.t("Invalid resolution.")
            color: themeVM.onSurface
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "resampleFromVideo"
                text: i18n.t("From Video")
                enabled: resampleVM.videoWidth > 0 && resampleVM.videoHeight > 0
                onClicked: {
                    targetWidth.text = String(resampleVM.videoWidth)
                    targetHeight.text = String(resampleVM.videoHeight)
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: i18n.t("Cancel")
                onClicked: root.close()
            }
            Button {
                id: applyButton
                objectName: "resampleApply"
                text: i18n.t("OK")
                highlighted: true
                enabled: targetWidth.acceptableInput && targetHeight.acceptableInput
                onClicked: root.confirm()
            }
        }
    }
}
