import QtQuick
import QtQuick.Controls.Material

Instantiator {
    id: root
    required property Menu targetMenu
    required property Component submenuFactory
    property var entries: []
    property int firstMacroIndex: 0
    model: root.entries

    delegate: QtObject {
        required property var modelData
        property var control: null
        property bool isSubmenu: false
    }
    onObjectAdded: (index, row) => {
        const entry = row.modelData
        const position = root.firstMacroIndex + index
        row.isSubmenu = !!(entry.children && entry.children.length)
        if (row.isSubmenu) {
            row.control = root.submenuFactory.createObject(root.targetMenu, {
                objectName: "automationMacroSubmenu",
                title: entry.title,
                enabled: entry.enabled !== false
            })
            root.targetMenu.insertMenu(position, row.control)
            // Populate after createObject returns, so deeper levels can reuse the factory.
            row.control.entries = entry.children
        } else {
            row.control = macroItem.createObject(root.targetMenu.contentItem, { entry: entry })
            root.targetMenu.insertItem(position, row.control)
        }
    }
    onObjectRemoved: (index, row) => {
        if (!row.control || !root.targetMenu)
            return
        if (row.isSubmenu)
            root.targetMenu.removeMenu(row.control)
        else
            root.targetMenu.removeItem(row.control)
    }

    property Component macroItem: Component {
        ShortcutMenuItem {
            id: item
            required property var entry
            objectName: "automationMacro_" + entry.id
            text: entry.title
            enabled: entry.enabled !== false
            checkable: entry.checked === true
            checked: entry.checked === true
            Accessible.description: entry.description || ""
            ToolTip {
                visible: root.targetMenu.visible && item.hovered && text.length > 0
                delay: 600
                text: item.entry.description || ""
            }
            onTriggered: {
                const macroId = entry.id
                checked = entry.checked === true
                Qt.callLater(() => automationVM.runMacro(macroId))
            }
        }
    }
}
