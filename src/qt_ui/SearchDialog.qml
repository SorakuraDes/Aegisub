import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Non-modal Find & Replace window. Reused for both Edit -> Find and
// Edit -> Replace; focusField() puts the caret in the right input.
Window {
    id: searchWindow
    width: 480
    height: 286
    minimumWidth: 420
    minimumHeight: 270
    title: i18n.t("Find & Replace")
    color: "#1e1e1e"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    readonly property int fieldHeight: 32

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
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: i18n.t("Find")
                font.pixelSize: 11
                color: "#9a9a9a"
                Layout.preferredWidth: 52
            }

            TextField {
                id: findField
                Layout.fillWidth: true
                implicitHeight: searchWindow.fieldHeight
                font.pixelSize: 13
                text: searchVM.query
                onTextEdited: searchVM.query = text
                onAccepted: searchVM.findNext()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: i18n.t("Replace")
                font.pixelSize: 11
                color: "#9a9a9a"
                Layout.preferredWidth: 52
            }

            TextField {
                id: replaceField
                Layout.fillWidth: true
                implicitHeight: searchWindow.fieldHeight
                font.pixelSize: 13
                text: searchVM.replacement
                onTextEdited: searchVM.replacement = text
                onAccepted: searchVM.replaceCurrent()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: i18n.t("Field")
                font.pixelSize: 11
                color: "#9a9a9a"
                Layout.preferredWidth: 52
            }

            ComboBox {
                id: fieldCombo
                implicitWidth: 120
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

            Item { Layout.preferredWidth: 52 }

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
            text: searchVM.statusMessage
            font.pixelSize: 11
            color: searchVM.statusMessage.indexOf("Invalid") >= 0 ? "#ff9a9a" : "#9a9a9a"
            elide: Text.ElideRight
        }

        Item { Layout.fillHeight: true }
    }
}
