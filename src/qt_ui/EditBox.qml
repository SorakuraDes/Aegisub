import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: editBox
    color: "#1b1b1b"
    radius: 6
    implicitHeight: 150

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
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                placeholderText: "0:00:00.00"
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
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                placeholderText: "0:00:00.00"
                onTextEdited: editBoxVM.endTime = text
            }

            Item { Layout.fillWidth: true }

            Label {
                text: gridModel.activeRow >= 0
                      ? i18n.t("Row") + " " + (gridModel.activeRow + 1)
                      : i18n.t("No line selected")
                font.pixelSize: 12
                color: "#888888"
            }
        }

        TextArea {
            id: textArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            font.pixelSize: 14
            wrapMode: TextArea.Wrap
            enabled: gridModel.activeRow >= 0
            placeholderText: i18n.t("No line selected")
            onTextChanged: if (editBoxVM.text !== text) editBoxVM.text = text
            background: Rectangle {
                color: "#2a2a2a"
                radius: 4
            }
        }
    }

    // Reload the editors when the selection changes. Editing the fields must
    // not fight the user's cursor, so we only sync on selection changes.
    Connections {
        target: gridModel
        function onActiveRowChanged() {
            startField.text = editBoxVM.startTime
            endField.text = editBoxVM.endTime
            textArea.text = editBoxVM.text
        }
    }

    Component.onCompleted: {
        startField.text = editBoxVM.startTime
        endField.text = editBoxVM.endTime
        textArea.text = editBoxVM.text
    }
}
