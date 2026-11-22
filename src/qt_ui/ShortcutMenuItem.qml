import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

/// Compact menu entry with a right-aligned shortcut hint.
///
/// Qt Quick Controls' MenuItem never renders a shortcut column (unlike Qt
/// Widgets' QMenu), so the hint is drawn by this contentItem. The actual key
/// handling lives in the menu bar's Shortcut elements or in main.cpp's key
/// filters; `shortcutText` is display-only.
MenuItem {
    id: control

    /// Display-only hint such as "Ctrl+S"; empty hides the column.
    property string shortcutText: ""

    implicitHeight: 30
    leftPadding: 10
    rightPadding: 10

    contentItem: RowLayout {
        spacing: 28

        Label {
            Layout.fillWidth: true
            text: control.text
            font.pixelSize: 13
            font.family: control.font.family
            color: control.enabled ? "#e6e6e6" : "#6a6a6a"
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Label {
            visible: control.shortcutText.length > 0
            text: control.shortcutText
            font.pixelSize: 12
            color: control.enabled ? "#8a8a8a" : "#5a5a5a"
            verticalAlignment: Text.AlignVCenter
        }
    }
}
