import QtQuick
import QtQuick.Controls.Material

Button {
    id: control
    property bool primary: false
    readonly property color fillColor: !enabled ? themeVM.surfaceContainerHighest
        : primary ? (pressed ? Qt.darker(themeVM.primary, 1.1) : themeVM.primary)
        : pressed ? themeVM.secondaryContainer : themeVM.surfaceContainerHighest

    background: Rectangle {
        implicitWidth: 64
        implicitHeight: 34
        radius: themeVM.radiusMedium
        color: control.fillColor
        border.width: control.visualFocus ? 2 : (!control.primary || !control.enabled ? 1 : 0)
        border.color: control.visualFocus ? (control.primary ? themeVM.onPrimary : themeVM.primary) : themeVM.outlineVariant
    }
    contentItem: Label {
        text: control.text
        font: control.font
        color: !control.enabled ? themeVM.onSurfaceVariant : control.primary ? themeVM.onPrimary : themeVM.onSurface
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText
    }
}
