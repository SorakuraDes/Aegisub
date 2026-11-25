import QtQuick
import QtQuick.Controls.Material
import QtQuick.Controls.impl as ControlsImpl
import QtQuick.Layouts

MenuItem {
    id: control

    // Display-only; shortcuts are handled by the menu bar or main.cpp.
    property string shortcutText: ""

    implicitHeight: 30
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    font.pixelSize: 13
    icon.width: 16
    icon.height: 16
    icon.color: enabled ? themeVM.onSurface : themeVM.outline
    indicator: null

    contentItem: RowLayout {
        spacing: 8

        // Reserve the same slot even for entries without an icon or checkmark.
        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: 16
            Layout.preferredWidth: 16
            Layout.maximumWidth: 16
            spacing: 0

            Rectangle {
                visible: control.checkable
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                implicitWidth: 14
                implicitHeight: 14
                radius: 2
                color: control.checked ? themeVM.primary : "transparent"
                border.color: control.checked ? themeVM.primary : themeVM.outline
                opacity: control.enabled ? 1 : 0.5

                Text {
                    anchors.fill: parent
                    visible: control.checked
                    text: "\u2713"
                    font.family: "Segoe UI Symbol"
                    font.pixelSize: 11
                    font.bold: true
                    color: themeVM.onPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            ControlsImpl.IconImage {
                visible: !control.checkable && control.icon.source.toString().length > 0
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                source: control.icon.source
                sourceSize: Qt.size(16, 16)
                color: control.icon.color
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.rightMargin: control.subMenu && control.arrow ? control.arrow.width + 8 : 0
            Layout.alignment: Qt.AlignVCenter
            text: control.text
            font: control.font
            color: control.enabled ? themeVM.onSurface : themeVM.outline
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Label {
            visible: control.shortcutText.length > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 20
            text: control.shortcutText
            font: control.font
            color: control.enabled ? themeVM.onSurfaceVariant : themeVM.outline
            verticalAlignment: Text.AlignVCenter
        }
    }
}
