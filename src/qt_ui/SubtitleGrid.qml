import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: grid
    color: themeVM.surface
    radius: themeVM.radiusMedium

    readonly property real gridFontSize: Math.max(3, preferencesVM.gridFontSize)
    readonly property int rowHeight: Math.max(26, Math.ceil(gridMetrics.height + 6))
    readonly property int colRow: Math.max(42, Math.ceil(monoMetrics.advanceWidth("0000") + 4))
    readonly property int colTime: Math.max(92, Math.ceil(monoMetrics.advanceWidth("0:00:00.00") + 4))
    readonly property int colLayer: 52
    readonly property int colStyle: 110
    readonly property int colActor: 110
    readonly property int colEffect: 90
    readonly property string monoFont: "Consolas"

    Binding {
        target: gridModel
        property: "hideOverridesChar"
        value: preferencesVM.hideOverridesChar
    }
    FontMetrics {
        id: gridMetrics
        font.pointSize: grid.gridFontSize
    }
    FontMetrics {
        id: monoMetrics
        font.family: grid.monoFont
        font.pointSize: grid.gridFontSize
    }

    // Row context menu. Declared here (not in the delegate) so a single menu
    // instance is reused; the row is selected before it opens, so the items can
    // simply act on gridModel.activeRow.
    Menu {
        id: rowMenu

        CompactIconMenuItem {
            text: i18n.t("Insert Before")
            icon.source: "qrc:/icons/add.svg"
            icon.color: themeVM.onSurface
            onClicked: gridModel.insertBefore(gridModel.activeRow)
        }
        CompactIconMenuItem {
            text: i18n.t("Insert After")
            icon.source: "qrc:/icons/add.svg"
            icon.color: themeVM.onSurface
            onClicked: gridModel.insertAfter(gridModel.activeRow)
        }
        MenuSeparator {}
        CompactIconMenuItem {
            text: i18n.t("Duplicate")
            icon.source: "qrc:/icons/content_copy.svg"
            icon.color: themeVM.onSurface
            enabled: gridModel.selectionCount > 0
            onClicked: gridModel.duplicateSelectedRows()
        }
        CompactIconMenuItem {
            text: i18n.t("Delete")
            icon.source: "qrc:/icons/delete.svg"
            icon.color: themeVM.onSurface
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
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("Start")
                Layout.preferredWidth: grid.colTime
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("End")
                Layout.preferredWidth: grid.colTime
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("Layer")
                Layout.preferredWidth: grid.colLayer
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("Style")
                Layout.preferredWidth: grid.colStyle
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("Actor")
                Layout.preferredWidth: grid.colActor
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("Effect")
                Layout.preferredWidth: grid.colEffect
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
            Label {
                text: i18n.t("Text")
                Layout.fillWidth: true
                font.pixelSize: 11
                font.bold: true
                color: themeVM.onSurfaceVariant
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: themeVM.outlineVariant
        }

        ListView {
            id: listView
            objectName: "subtitleGridList"
            property bool restoringScroll: false
            property int savedScrollRow: 0
            function restoreScroll() {
                if (count > 0)
                    positionViewAtIndex(Math.min(savedScrollRow, count - 1), ListView.Beginning)
                restoringScroll = false
            }
            onContentYChanged: {
                if (!restoringScroll)
                    documentVM.setGridScrollPosition(Math.floor(Math.max(0, contentY - originY) / grid.rowHeight))
            }
            Connections {
                target: documentVM
                function onDocumentAboutToReload() {
                    listView.restoringScroll = true
                    // Cancel outstanding delegate requests against the old model.
                    listView.model = null
                }
                function onDocumentReloaded() {
                    listView.savedScrollRow = documentVM.gridScrollPosition()
                    listView.model = gridModel
                    Qt.callLater(listView.restoreScroll)
                }
            }
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
                color: isActive ? themeVM.rowActive : isSelected ? themeVM.rowSelected
                       : (index % 2 === 0 ? themeVM.surfaceContainerLow : themeVM.rowAlt)

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
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.primary
                    }
                    Label {
                        text: model.startTime
                        Layout.preferredWidth: grid.colTime
                        font.family: grid.monoFont
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurface
                    }
                    Label {
                        text: model.endTime
                        Layout.preferredWidth: grid.colTime
                        font.family: grid.monoFont
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurface
                    }
                    Label {
                        text: model.layer
                        Layout.preferredWidth: grid.colLayer
                        font.family: grid.monoFont
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurface
                    }
                    Label {
                        text: model.styleName
                        Layout.preferredWidth: grid.colStyle
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurface
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.actor
                        Layout.preferredWidth: grid.colActor
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurfaceVariant
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.effect
                        Layout.preferredWidth: grid.colEffect
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurfaceVariant
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "subtitleGridText"
                        text: model.displayText
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        font.pointSize: grid.gridFontSize
                        color: model.isComment ? themeVM.rowComment : themeVM.onSurface
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
