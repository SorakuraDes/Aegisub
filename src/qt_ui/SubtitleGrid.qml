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
                color: isActive
                       ? Qt.rgba(Material.color(Material.Blue).r,
                                 Material.color(Material.Blue).g,
                                 Material.color(Material.Blue).b, 0.35)
                       : (index % 2 === 0 ? "#1f1f1f" : "#242424")

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: gridModel.activeRow = index
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
