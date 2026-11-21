import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    color: "#1b1b1b"
    radius: 6
    implicitHeight: 150

    function loadSelection() {
        startField.text = editBoxVM.startTime
        endField.text = editBoxVM.endTime
        textArea.text = editBoxVM.text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: i18n.t("Start")
                font.pixelSize: 12
                color: "#9a9a9a"
            }

            TextField {
                id: startField
                Layout.preferredWidth: 150
                implicitHeight: 34
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                onTextEdited: editBoxVM.startTime = text
            }

            Label {
                text: i18n.t("End")
                font.pixelSize: 12
                color: "#9a9a9a"
            }

            TextField {
                id: endField
                Layout.preferredWidth: 150
                implicitHeight: 34
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                onTextEdited: editBoxVM.endTime = text
            }

            Item { Layout.fillWidth: true }

            Label {
                text: gridModel.activeRow >= 0
                      ? "#" + (gridModel.activeRow + 1)
                      : i18n.t("No line selected")
                font.pixelSize: 12
                color: "#888888"
            }
        }

        // The panel lives on the ScrollView so the TextArea can keep its own
        // (zero-inset) geometry: text then always stays inside the background.
        ScrollView {
            id: textScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            background: Rectangle {
                color: "#2a2a2a"
                radius: 4
            }

            TextArea {
                id: textArea
                width: textScroll.availableWidth
                height: implicitHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 10
                rightPadding: 10
                topPadding: 8
                bottomPadding: 8
                font.pixelSize: 14
                wrapMode: TextArea.Wrap
                enabled: gridModel.activeRow >= 0
                placeholderText: i18n.t("No line selected")
                onTextChanged: if (editBoxVM.text !== text) editBoxVM.text = text
                background: null
            }
        }
    }

    // Reload the editors when the selection changes or the model is reset
    // (new/open document). Editing the fields must not fight the user's
    // cursor, so we do not bind them to the view model properties.
    Connections {
        target: gridModel
        function onActiveRowChanged() { loadSelection() }
        function onModelReset() { loadSelection() }
    }

    // Undo/redo (and any commit made by another editor) replaces the active
    // line's contents, so the fields have to be refilled explicitly.
    Connections {
        target: editBoxVM
        function onExternalReload() { loadSelection() }
    }

    Component.onCompleted: loadSelection()
}
