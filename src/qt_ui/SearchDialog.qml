import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Non-modal Find & Replace window. Reused for both Edit -> Find and
// Edit -> Replace; focusField() puts the caret in the right input.
Window {
    id: searchWindow
    width: 480
    height: 268
    minimumWidth: Math.max(420, searchForm.implicitWidth + 2 * searchForm.anchors.margins)
    minimumHeight: searchForm.implicitHeight + 2 * searchForm.anchors.margins
    title: i18n.t("Find & Replace")
    color: themeVM.surface

    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    readonly property int fieldHeight: themeVM.controlHeight
    readonly property real labelWidth: Math.max(findLabel.implicitWidth, replaceLabel.implicitWidth, fieldLabel.implicitWidth)

    function focusField(useReplace) {
        if (useReplace) {
            replaceField.forceActiveFocus()
            replaceField.selectAll()
        } else {
            findField.forceActiveFocus()
            findField.selectAll()
        }
    }

    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        onActivated: searchWindow.close()
    }

    ColumnLayout {
        id: searchForm
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                id: findLabel
                text: i18n.t("Find")
                font.pixelSize: 12
                color: themeVM.onSurface
                Layout.preferredWidth: searchWindow.labelWidth
            }

            TextField {
                id: findField
                Layout.fillWidth: true
                implicitHeight: searchWindow.fieldHeight
                font.pixelSize: 13
                Accessible.name: findLabel.text
                text: searchVM.query
                onTextEdited: searchVM.query = text
                onAccepted: searchVM.findNext()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                id: replaceLabel
                text: i18n.t("Replace")
                font.pixelSize: 12
                color: themeVM.onSurface
                Layout.preferredWidth: searchWindow.labelWidth
            }

            TextField {
                id: replaceField
                Layout.fillWidth: true
                implicitHeight: searchWindow.fieldHeight
                font.pixelSize: 13
                Accessible.name: replaceLabel.text
                text: searchVM.replacement
                onTextEdited: searchVM.replacement = text
                onAccepted: searchVM.replaceCurrent()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                id: fieldLabel
                text: i18n.t("Field")
                font.pixelSize: 12
                color: themeVM.onSurface
                Layout.preferredWidth: searchWindow.labelWidth
            }

            ComboBox {
                id: fieldCombo
                Accessible.name: fieldLabel.text
                Layout.minimumWidth: 120
                implicitContentWidthPolicy: ComboBox.WidestText
                implicitHeight: searchWindow.fieldHeight
                font.pixelSize: 12
                model: [i18n.t("Text"), i18n.t("Actor"), i18n.t("Effect"), i18n.t("Style")]
                currentIndex: searchVM.searchField
                onActivated: searchVM.searchField = currentIndex
            }

            CheckBox {
                text: i18n.t("Case sensitive")
                font.pixelSize: 12
                checked: searchVM.caseSensitive
                onToggled: searchVM.caseSensitive = checked
            }

            CheckBox {
                text: i18n.t("Regex")
                font.pixelSize: 12
                checked: searchVM.useRegex
                onToggled: searchVM.useRegex = checked
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.preferredWidth: searchWindow.labelWidth }

            CheckBox {
                text: i18n.t("In selection")
                font.pixelSize: 12
                checked: searchVM.inSelectionOnly
                onToggled: searchVM.inSelectionOnly = checked
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: i18n.t("Find Next")
                icon.source: "qrc:/icons/search.svg"
                icon.color: themeVM.onPrimary
                icon.width: 18
                icon.height: 18
                padding: 6
                leftPadding: 10
                rightPadding: 10
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                palette.buttonText: themeVM.onPrimary
                highlighted: true
                onClicked: searchVM.findNext()
            }
            Button {
                text: i18n.t("Find Prev")
                onClicked: searchVM.findPrev()
            }
            Button {
                text: i18n.t("Replace")
                onClicked: searchVM.replaceCurrent()
            }
            Button {
                text: i18n.t("Replace All")
                onClicked: searchVM.replaceAll()
            }

            Item { Layout.fillWidth: true }
        }

        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            text: searchVM.statusMessage
            font.pixelSize: 12
            color: searchVM.statusMessage.indexOf("Invalid") >= 0 ? themeVM.danger : themeVM.onSurfaceVariant
            elide: Text.ElideRight
        }

        Item { Layout.fillHeight: true }
    }
}
