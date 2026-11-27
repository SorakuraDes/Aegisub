import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: grid
    readonly property string hotkeyContext: "Subtitle Grid"
    color: themeVM.surface
    radius: themeVM.radiusMedium

    readonly property real gridFontSize: Math.max(3, preferencesVM.gridFontSize)
    readonly property string gridFontFamily: preferencesVM.gridFontFace || "Tahoma"
    readonly property var gridColors: preferencesVM.gridColors
    readonly property int cpsWarning: preferencesVM.cpsWarningThreshold
    readonly property int cpsError: Math.max(cpsWarning, preferencesVM.cpsErrorThreshold)
    readonly property int colCps: Math.max(42, Math.ceil(gridMetrics.advanceWidth("999") + 12))

    function cpsFraction(cps) {
        return cps > cpsWarning ? Math.min(1, (cps - cpsWarning + 1) / (cpsError - cpsWarning + 1)) : 0
    }
    readonly property int rowHeight: Math.max(26, Math.ceil(gridMetrics.height + 6))
    readonly property int colFold: 20
    readonly property int foldCountWidth: 32
    readonly property int colRow: Math.max(42, Math.ceil(gridMetrics.advanceWidth("0000") + 4))
                                  + foldCountWidth + 3

    // Values supplied by the model's foldRole role.
    readonly property int openFoldStart: 1
    readonly property int closedFoldStart: 2
    readonly property int foldEnd: 4
    readonly property int colTime: Math.max(92, Math.ceil(gridMetrics.advanceWidth("0:00:00.00") + 4))
    readonly property int colLayer: 52
    readonly property int colStyle: 110
    readonly property int colActor: 110
    readonly property int colEffect: 90

    Binding {
        target: gridModel
        property: "hideOverridesChar"
        value: preferencesVM.hideOverridesChar
    }
    FontMetrics {
        id: gridMetrics
        font.family: grid.gridFontFamily
        font.pointSize: grid.gridFontSize
    }

    // Row context menu. Declared here (not in the delegate) so a single menu
    // instance is reused. Selection actions preserve an existing multi-selection;
    // insertion is anchored to the document row that opened the menu.
    Menu {
        id: rowMenu
        objectName: "subtitleGridRowMenu"
        property int contextDocRow: -1

        CompactIconMenuItem {
            text: i18n.t("Insert Before")
            icon.source: "qrc:/icons/add.svg"
            icon.color: themeVM.onSurface
            onClicked: gridModel.insertBefore(rowMenu.contextDocRow)
        }
        CompactIconMenuItem {
            text: i18n.t("Insert After")
            icon.source: "qrc:/icons/add.svg"
            icon.color: themeVM.onSurface
            onClicked: gridModel.insertAfter(rowMenu.contextDocRow)
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
        MenuSeparator {}
        ShortcutMenuItem {
            text: i18n.t("Create new Fold")
            enabled: gridModel.selectionCount >= 2
            onTriggered: gridModel.createFoldFromSelection()
        }
        ShortcutMenuItem {
            text: i18n.t("Toggle Folds")
            enabled: gridModel.selectionHasFolds
            onTriggered: gridModel.toggleFoldsForSelection()
        }
        ShortcutMenuItem {
            text: i18n.t("Clear Folds")
            enabled: gridModel.selectionHasFolds
            onTriggered: gridModel.clearFoldsForSelection()
        }
    }

    // The search dialog reports a document row, not a ListView index.
    Connections {
        target: searchVM
        function onLocateRow(row) {
            listView.locateDocRow(row)
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

            Item {
                Layout.minimumWidth: grid.colFold
                Layout.maximumWidth: grid.colFold
            }
            Label {
                text: "#"
                Layout.minimumWidth: grid.colRow
                Layout.maximumWidth: grid.colRow
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
                text: i18n.t("CPS")
                Layout.minimumWidth: grid.colCps
                Layout.maximumWidth: grid.colCps
                font.pixelSize: 11
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
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
            property int savedVisibleRow: 0

            function restoreScroll() {
                if (count > 0)
                    positionViewAtIndex(Math.min(savedVisibleRow, count - 1), ListView.Beginning)
                restoringScroll = false
            }
            function locateDocRow(docRow) {
                if (restoringScroll || docRow < 0)
                    return
                const visibleRow = gridModel.visibleRowForDocRow(docRow)
                if (visibleRow >= 0) {
                    forceLayout()
                    positionViewAtIndex(visibleRow, ListView.Contain)
                }
            }
            function locateActiveRow() {
                locateDocRow(gridModel.activeRow)
            }
            Connections {
                target: gridModel
                function onActiveRowChanged() {
                    if (!listView.restoringScroll)
                        Qt.callLater(listView.locateActiveRow)
                }
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
                    // Native scroll_position already stores a visible index.
                    listView.savedVisibleRow = documentVM.gridScrollPosition()
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
                objectName: "subtitleGridRow"
                width: listView.width
                height: grid.rowHeight
                readonly property int docRow: model.docRow
                readonly property bool isActive: model.isActive
                readonly property bool isSelected: model.isSelected
                readonly property bool foldClosed: model.foldRole === grid.closedFoldStart
                readonly property bool foldStart: model.foldRole === grid.openFoldStart || foldClosed
                readonly property bool foldEnd: model.foldRole === grid.foldEnd
                readonly property color defaultBackground: isActive ? themeVM.rowActive : isSelected ? themeVM.rowSelected
                                                           : (index % 2 === 0 ? themeVM.surfaceContainerLow : themeVM.rowAlt)
                readonly property var foregroundOverride: grid.gridColors[isSelected ? "Selection" : "Standard"]
                color: (isSelected ? grid.gridColors["Background/Selection"]
                                  : model.isComment ? grid.gridColors["Background/Comment"]
                                                    : grid.gridColors["Background/Background"])
                       || defaultBackground

                Rectangle {
                    anchors.fill: parent
                    z: 1
                    visible: rowDelegate.isActive && grid.gridColors["Active Border"] !== undefined
                    color: "transparent"
                    border.color: grid.gridColors["Active Border"] || "transparent"
                }

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
                            const pos = rowDelegate.mapToItem(grid, mouse.x, mouse.y)
                            rowMenu.contextDocRow = rowDelegate.docRow
                            if (!rowDelegate.isSelected)
                                gridModel.selectSingle(rowMenu.contextDocRow)
                            rowMenu.popup(pos.x, pos.y)
                            return
                        }

                        if (mouse.modifiers & Qt.ShiftModifier)
                            gridModel.selectRangeVisible(index)
                        else if (mouse.modifiers & Qt.ControlModifier)
                            gridModel.toggleSelect(rowDelegate.docRow)
                        else
                            gridModel.selectSingle(rowDelegate.docRow)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Item {
                        id: foldGutter
                        Layout.minimumWidth: grid.colFold
                        Layout.maximumWidth: grid.colFold
                        Layout.fillHeight: true

                        // Deeply nested folds share the last lane so the marker
                        // always fits inside the fixed-width gutter.
                        readonly property int laneLimit: 2
                        readonly property int laneSpacing: 3
                        readonly property int markerX: 6 + Math.min(model.foldDepth, laneLimit) * laneSpacing
                        readonly property int guideCount: Math.min(model.foldDepth, laneLimit + 1)

                        Repeater {
                            model: foldGutter.guideCount

                            Item {
                                required property int index
                                readonly property bool endsHere: rowDelegate.foldEnd
                                                                   && index === foldGutter.guideCount - 1
                                x: 6 + index * foldGutter.laneSpacing
                                width: 1
                                height: foldGutter.height

                                Rectangle {
                                    width: 1
                                    height: parent.endsHere ? Math.floor(parent.height / 2) + 1 : parent.height
                                    color: themeVM.outline
                                }
                                Rectangle {
                                    visible: parent.endsHere
                                    y: Math.floor(parent.height / 2)
                                    width: foldGutter.laneSpacing
                                    height: 1
                                    color: themeVM.outline
                                }
                            }
                        }
                        Rectangle {
                            visible: rowDelegate.foldStart && !rowDelegate.foldClosed
                            x: foldGutter.markerX
                            y: Math.floor(parent.height / 2)
                            width: 1
                            height: parent.height - y
                            color: themeVM.outline
                        }
                        Button {
                            id: foldMarker
                            objectName: "subtitleGridFoldMarker"
                            visible: rowDelegate.foldStart
                            x: foldGutter.markerX - 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 13
                            height: 14
                            padding: 0
                            topInset: 0
                            bottomInset: 0
                            leftInset: 0
                            rightInset: 0
                            focusPolicy: Qt.NoFocus
                            hoverEnabled: true
                            text: rowDelegate.foldClosed ? "+" : "\u2212"
                            Accessible.name: rowDelegate.foldClosed ? i18n.t("Open Fold") : i18n.t("Close Fold")
                            ToolTip.text: Accessible.name
                            ToolTip.visible: hovered
                            ToolTip.delay: 450
                            background: Rectangle {
                                radius: themeVM.radiusSmall
                                border.color: foldMarker.hovered ? themeVM.primary : themeVM.outline
                                color: grid.gridColors[rowDelegate.foldClosed ? "Background/Closed Fold" : "Background/Open Fold"]
                                       || (foldMarker.down ? themeVM.surfaceContainerHighest
                                           : foldMarker.hovered || rowDelegate.foldClosed ? themeVM.primaryContainer
                                           : themeVM.surfaceContainer)
                            }
                            contentItem: Text {
                                text: foldMarker.text
                                color: foldMarker.hovered || rowDelegate.foldClosed ? themeVM.onPrimaryContainer
                                       : themeVM.onSurface
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                listView.forceActiveFocus()
                                gridModel.toggleFoldAtDocRow(rowDelegate.docRow)
                            }
                        }
                    }
                    RowLayout {
                        Layout.minimumWidth: grid.colRow
                        Layout.maximumWidth: grid.colRow
                        spacing: 3

                        Label {
                            objectName: "subtitleGridRowNumber"
                            text: rowDelegate.docRow + 1
                            Layout.fillWidth: true
                            font.family: grid.gridFontFamily
                            font.pointSize: grid.gridFontSize
                            color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.primary)
                        }
                        Label {
                            objectName: "subtitleGridFoldHiddenCount"
                            visible: rowDelegate.foldClosed && model.foldedHiddenCount > 0
                            text: "+" + model.foldedHiddenCount
                            Layout.preferredWidth: grid.foldCountWidth
                            Layout.maximumWidth: grid.foldCountWidth
                            font.pixelSize: 10
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 7
                            horizontalAlignment: Text.AlignRight
                            color: themeVM.primary
                        }
                    }
                    Label {
                        text: model.startTime
                        Layout.preferredWidth: grid.colTime
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurface)
                    }
                    Label {
                        text: model.endTime
                        Layout.preferredWidth: grid.colTime
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurface)
                    }
                    Label {
                        text: model.layer
                        Layout.preferredWidth: grid.colLayer
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurface)
                    }
                    Label {
                        text: model.styleName
                        Layout.preferredWidth: grid.colStyle
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurface)
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.actor
                        Layout.preferredWidth: grid.colActor
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurfaceVariant)
                        elide: Text.ElideRight
                    }
                    Label {
                        text: model.effect
                        Layout.preferredWidth: grid.colEffect
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurfaceVariant)
                        elide: Text.ElideRight
                    }
                    Item {
                        Layout.minimumWidth: grid.colCps
                        Layout.maximumWidth: grid.colCps
                        Layout.fillHeight: true
                        readonly property int cps: model.cps
                        readonly property real warningFraction: grid.cpsFraction(cps)
                        readonly property color warningColor: grid.gridColors["CPS Error"] || themeVM.danger
                        readonly property color normalText: rowDelegate.foregroundOverride
                                                            || (model.isComment ? themeVM.rowComment : themeVM.onSurface)

                        Rectangle {
                            objectName: "subtitleGridCpsBackground"
                            anchors.fill: parent
                            opacity: parent.warningFraction
                            color: parent.warningColor
                        }
                        Label {
                            objectName: "subtitleGridCps"
                            anchors.centerIn: parent
                            text: parent.cps < 0 ? "" : String(parent.cps)
                            font.family: grid.gridFontFamily
                            font.pointSize: grid.gridFontSize
                            // Original CPS text fades to black as the warning saturates.
                            color: Qt.rgba(parent.normalText.r * (1 - parent.warningFraction),
                                           parent.normalText.g * (1 - parent.warningFraction),
                                           parent.normalText.b * (1 - parent.warningFraction), 1)
                        }
                    }
                    Label {
                        objectName: "subtitleGridText"
                        text: model.displayText
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        font.family: grid.gridFontFamily
                        font.pointSize: grid.gridFontSize
                        color: rowDelegate.foregroundOverride || (model.isComment ? themeVM.rowComment : themeVM.onSurface)
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
