import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: grid
    color: "#1b1b1b"
    radius: 6

    readonly property int rowHeight: 26
    readonly property int colRow: 42
    readonly property int colTime: 92
    readonly property int colLayer: 52
    readonly property int colStyle: 110
    readonly property int colActor: 110
    readonly property int colEffect: 90
    readonly property string monoFont: "Consolas"

    // Row context menu. Declared here (not in the delegate) so a single menu
    // instance is reused; the row is selected before it opens, so the items can
    // simply act on gridModel.activeRow.
    Menu {
        id: rowMenu

        MenuItem {
            text: i18n.t("Insert Before")
            onClicked: gridModel.insertBefore(gridModel.activeRow)
        }
        MenuItem {
            text: i18n.t("Insert After")
            onClicked: gridModel.insertAfter(gridModel.activeRow)
        }
        MenuSeparator {}
        MenuItem {
            text: i18n.t("Duplicate")
            enabled: gridModel.selectionCount > 0
            onClicked: gridModel.duplicateSelectedRows()
        }
        MenuItem {
            text: i18n.t("Delete")
            enabled: gridModel.selectionCount > 0
            onClicked: gridModel.deleteSelectedRows()
        }
    }

    // The search dialog asks the grid to bring a match into view.
    Connections {
        target: searchVM
        function onLocateRow(row) {
            if (row >= 0)
                listView.positionViewAtIndex(row, ListView.Contain)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "#"
                Layout.preferredWidth: grid.colRow
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("Start")
                Layout.preferredWidth: grid.colTime
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("End")
                Layout.preferredWidth: grid.colTime
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("Layer")
                Layout.preferredWidth: grid.colLayer
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("Style")
                Layout.preferredWidth: grid.colStyle
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("Actor")
                Layout.preferredWidth: grid.colActor
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("Effect")
                Layout.preferredWidth: grid.colEffect
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
            Label {
                text: i18n.t("Text")
                Layout.fillWidth: true
                font.pixelSize: 11
                font.bold: true
                color: "#9a9a9a"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#3a3a3a"
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: gridModel
            reuseItems: true
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: rowDelegate
                width: listView.width
                height: grid.rowHeight
                property bool isActive: index === gridModel.activeRow
                property bool isSelected: model.isSelected === true
                color: isActive
                       ? Qt.rgba(Material.color(Material.Blue).r,
                                 Material.color(Material.Blue).g,
                                 Material.color(Material.Blue).b, 0.35)
                       : (isSelected
                          ? Qt.rgba(Material.color(Material.Blue).r,
                                    Material.color(Material.Blue).g,
                                    Material.color(Material.Blue).b, 0.18)
                          : (index % 2 === 0 ? "#1f1f1f" : "#242424"))

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        // Clicking the grid takes keyboard focus away from the
                        // EditBox fields so that Delete/Insert/Ctrl+D act on
                        // rows, and Ctrl+A selects rows instead of text.
                        listView.forceActiveFocus()

                        if (mouse.button === Qt.RightButton) {
                            // Right-clicking a row that is already part of the
                            // selection keeps that selection; otherwise make the
                            // row the selection first.
                            if (!rowDelegate.isSelected)
                                gridModel.selectSingle(index)
                            var pos = rowDelegate.mapToItem(grid, mouse.x, mouse.y)
                            rowMenu.popup(pos.x, pos.y)
                            return
                        }

                        if (mouse.modifiers & Qt.ShiftModifier)
                            gridModel.selectRange(index)
                        else if (mouse.modifiers & Qt.ControlModifier)
                            gridModel.toggleSelect(index)
                        else
                            gridModel.selectSingle(index)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: model.rowNumber
                        Layout.preferredWidth: grid.colRow
                        font.family: grid.monoFont
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#6f9fdf"
                    }
                    Label {
                        text: model.startTime
                        Layout.preferredWidth: grid.colTime
                        font.family: grid.monoFont
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#cfcfcf"
                    }
                    Label {
                        text: model.endTime
                        Layout.preferredWidth: grid.colTime
                        font.family: grid.monoFont
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#cfcfcf"
                    }
                    Label {
                        text: model.layer
                        Layout.preferredWidth: grid.colLayer
                        font.family: grid.monoFont
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#cfcfcf"
                    }
                    Label {
                        text: model.styleName
                        Layout.preferredWidth: grid.colStyle
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#dcdcdc"
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.actor
                        Layout.preferredWidth: grid.colActor
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#bdbdbd"
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.effect
                        Layout.preferredWidth: grid.colEffect
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#bdbdbd"
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.text
                        Layout.fillWidth: true
                        font.pixelSize: 12
                        color: model.isComment ? "#8a8a8a" : "#eeeeee"
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
