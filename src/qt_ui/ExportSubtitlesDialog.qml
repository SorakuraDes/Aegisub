import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Dialog {
    id: dialog
    objectName: "exportSubtitlesDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    title: i18n.t("Export")
    width: Math.max(0, Math.min(620, parent ? parent.width - 32 : 620))
    height: Math.max(0, Math.min(480, parent ? parent.height - 32 : 480))
    Material.theme: themeVM.darkMode ? Material.Dark : Material.Light
    Material.accent: themeVM.primary
    Material.background: themeVM.surfaceContainer
    Material.foreground: themeVM.onSurface

    readonly property var selectedFilter: filterList.currentItem

    ListModel { id: filterModel }

    onAboutToShow: {
        filterModel.clear()
        refreshFilters()
        filterList.currentIndex = filterModel.count ? 0 : -1
        const saved = encoding.find(automationVM.exportEncoding, Qt.MatchExactly)
        const utf8 = encoding.find("Unicode (UTF-8)", Qt.MatchExactly)
        encoding.currentIndex = saved >= 0 ? saved : utf8 >= 0 ? utf8 : 0
    }
    onOpened: filterList.forceActiveFocus()

    Connections {
        target: automationVM
        function onExportFiltersChanged() {
            if (dialog.visible)
                dialog.refreshFilters()
        }
    }

    function refreshFilters() {
        const previousIndex = filterList.currentIndex
        const currentId = previousIndex >= 0 && previousIndex < filterModel.count
                        ? filterModel.get(previousIndex).filterId : null
        const available = new Map()
        const filters = automationVM.exportFilters
        for (let i = 0; i < filters.length; ++i) {
            const filter = filters[i]
            available.set(filter.id, {filterId: filter.id, filterName: filter.name,
                                      description: filter.description || "", hasConfig: !!filter.hasConfig,
                                      isChecked: !!filter.checked})
        }

        // Keep surviving rows in the user's order with their current check states.
        for (let i = filterModel.count - 1; i >= 0; --i) {
            const previous = filterModel.get(i)
            const updated = available.get(previous.filterId)
            if (updated) {
                available.delete(previous.filterId)
                updated.isChecked = previous.isChecked
                filterModel.set(i, updated)
            } else {
                filterModel.remove(i)
            }
        }
        for (let i = 0; i < filters.length; ++i) {
            const added = available.get(filters[i].id)
            if (added)
                filterModel.append(added)
        }

        for (let i = 0; i < filterModel.count; ++i) {
            if (filterModel.get(i).filterId === currentId) {
                filterList.currentIndex = i
                return
            }
        }
        filterList.currentIndex = Math.min(Math.max(0, previousIndex), filterModel.count - 1)
    }

    function moveFilter(direction) {
        const from = filterList.currentIndex
        const to = from + direction
        if (from < 0 || to < 0 || to >= filterModel.count)
            return
        filterModel.move(from, to, 1)
        filterList.currentIndex = to
        filterList.positionViewAtIndex(to, ListView.Contain)
    }

    function setAll(checked) {
        for (let i = 0; i < filterModel.count; ++i)
            filterModel.setProperty(i, "isChecked", checked)
    }

    function orderedIds() {
        const ids = []
        for (let i = 0; i < filterModel.count; ++i) {
            const filter = filterModel.get(i)
            if (filter.isChecked)
                ids.push(filter.filterId)
        }
        return ids
    }

    contentItem: ColumnLayout {
        spacing: 6
        Label { text: i18n.t("Filters"); font.bold: true }
        ListView {
            id: filterList
            objectName: "exportFilterList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 48
            clip: true
            model: filterModel
            keyNavigationEnabled: true
            Accessible.name: i18n.t("Filters")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: filterRow
                required property int index
                required property int filterId
                required property string filterName
                required property string description
                required property bool hasConfig
                required property bool isChecked
                width: filterList.width
                padding: 0
                highlighted: ListView.isCurrentItem
                Accessible.name: filterName
                onClicked: filterList.currentIndex = index
                Keys.onSpacePressed: filterModel.setProperty(index, "isChecked", !isChecked)
                contentItem: RowLayout {
                    CheckBox {
                        objectName: "exportFilter_" + filterRow.filterId
                        checked: filterRow.isChecked
                        Accessible.name: filterRow.filterName
                        onToggled: {
                            filterList.currentIndex = filterRow.index
                            filterModel.setProperty(filterRow.index, "isChecked", checked)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.rightMargin: 12
                        text: filterRow.filterName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: filterList.count === 0
                text: i18n.t("Empty")
                color: themeVM.onSurfaceVariant
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            Button {
                objectName: "exportMoveUpButton"
                text: i18n.t("Move &Up")
                enabled: filterList.currentIndex > 0
                onClicked: dialog.moveFilter(-1)
            }
            Button {
                objectName: "exportMoveDownButton"
                text: i18n.t("Move &Down")
                enabled: filterList.currentIndex >= 0 && filterList.currentIndex + 1 < filterModel.count
                onClicked: dialog.moveFilter(1)
            }
            Button {
                objectName: "exportSelectAllButton"
                text: i18n.t("Select &All")
                enabled: filterModel.count > 0
                onClicked: dialog.setAll(true)
            }
            Button {
                objectName: "exportSelectNoneButton"
                text: i18n.t("Select &None")
                enabled: filterModel.count > 0
                onClicked: dialog.setAll(false)
            }
            Button {
                objectName: "exportConfigureButton"
                text: i18n.t("Configure...")
                enabled: !automationVM.busy && !!dialog.selectedFilter && dialog.selectedFilter.hasConfig
                onClicked: automationVM.configureExportFilter(dialog.selectedFilter.filterId)
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            contentWidth: availableWidth
            clip: true
            TextArea {
                objectName: "exportFilterDescription"
                readOnly: true
                selectByMouse: true
                text: dialog.selectedFilter ? dialog.selectedFilter.description : ""
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: i18n.t("Text encoding:") }
            ComboBox {
                id: encoding
                objectName: "exportEncodingCombo"
                Layout.fillWidth: true
                Accessible.name: i18n.t("Text encoding:")
                model: automationVM.exportEncodings
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "exportSaveButton"
            text: i18n.t("Export...")
            enabled: !automationVM.busy && encoding.currentIndex >= 0
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: target.open()
        }
        Button {
            objectName: "exportCancelButton"
            text: i18n.t("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    Dialogs.FileDialog {
        id: target
        objectName: "exportTargetFileDialog"
        title: i18n.t("Export subtitles file")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [i18n.t("Advanced SubStation Alpha") + " (*.ass)",
                      i18n.t("SubRip") + " (*.srt)"]
        defaultSuffix: selectedNameFilter.index === 1 ? "srt" : "ass"
        onAccepted: {
            const selectedEncoding = encoding.currentText
            const ids = dialog.orderedIds()
            const output = selectedFile
            dialog.close()
            automationVM.exportSubtitles(output, selectedEncoding, ids)
        }
    }
}
