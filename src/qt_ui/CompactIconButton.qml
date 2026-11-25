import QtQuick
import QtQuick.Controls.Material

Button {
    id: control
    flat: true
    display: AbstractButton.IconOnly
    implicitWidth: 30
    implicitHeight: 30
    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    icon.width: 18
    icon.height: 18
    icon.color: control.checked ? themeVM.onPrimaryContainer : themeVM.onSurface
    contentItem.opacity: enabled ? 1 : 0.5
    property string actionDescription: text
    Accessible.name: actionDescription
    ToolTip {
        visible: control.enabled && (control.hovered || control.visualFocus)
        text: control.actionDescription
        delay: 450
    }

    background: Rectangle {
        radius: themeVM.radiusSmall
        color: control.checked ? themeVM.primaryContainer
             : control.down ? themeVM.surfaceContainerHighest
             : control.hovered && control.enabled ? themeVM.surfaceContainerHigh : "transparent"
        border.color: control.visualFocus ? themeVM.primary : "transparent"
    }
}
