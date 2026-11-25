import QtQuick
import QtQuick.Controls.Material

Menu {
    id: root
    required property var paths
    signal openPath(string path)
    title: i18n.t("Recent")
    width: 400
    delegate: ShortcutMenuItem {}

    ShortcutMenuItem {
        text: i18n.t("Empty")
        enabled: false
        visible: root.paths.length === 0
        height: visible ? 30 : 0
    }

    Instantiator {
        model: root.paths
        delegate: ShortcutMenuItem {
            required property string modelData
            text: modelData
            onTriggered: root.openPath(modelData)
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
