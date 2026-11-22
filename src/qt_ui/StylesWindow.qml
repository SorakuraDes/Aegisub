import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Non-modal styles manager: a compact list/form editor for [V4+ Styles].
Window {
    id: stylesWindow
    width: 800
    height: 560
    minimumWidth: 720
    minimumHeight: 500
    title: i18n.t("Styles Manager")
    color: "#1e1e1e"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    readonly property int fieldHeight: 30

    // True while loadStyleForm() is assigning fields. Without it the controls'
    // initial values (SpinBox starts at `from`) fire their change handlers and
    // overwrite the style before the form is ever populated.
    property bool loadingForm: true

    function safeColor(value) {
        return /^#[0-9a-fA-F]{6}$/.test(value) ? value : "#000000"
    }

    function loadStyleForm() {
        if (stylesVM.currentIndex < 0)
            return

        loadingForm = true
        nameField.text = stylesVM.name
        fontField.text = stylesVM.fontName
        fontSizeSpin.value = Math.round(stylesVM.fontSize)
        boldCheck.checked = stylesVM.bold
        italicCheck.checked = stylesVM.italic
        underlineCheck.checked = stylesVM.underline
        strikeoutCheck.checked = stylesVM.strikeout

        primaryHex.text = stylesVM.primaryColor
        primaryAlphaSpin.value = stylesVM.primaryAlpha
        secondaryHex.text = stylesVM.secondaryColor
        secondaryAlphaSpin.value = stylesVM.secondaryAlpha
        outlineHex.text = stylesVM.outlineColor
        outlineAlphaSpin.value = stylesVM.outlineAlpha
        shadowHex.text = stylesVM.shadowColor
        shadowAlphaSpin.value = stylesVM.shadowAlpha

        borderCombo.currentIndex = stylesVM.borderStyle === 3 ? 1 : 0
        outlineSpin.value = Math.round(stylesVM.outlineWidth)
        shadowSpin.value = Math.round(stylesVM.shadowDepth)
        marginLSpin.value = stylesVM.marginL
        marginRSpin.value = stylesVM.marginR
        marginVSpin.value = stylesVM.marginV
        loadingForm = false
    }

    Shortcut {
        sequence: StandardKey.Cancel
        context: Qt.WindowShortcut
        onActivated: stylesWindow.close()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ---- Left: style list -------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            spacing: 6

            Label {
                text: i18n.t("Styles")
                font.pixelSize: 11
                color: "#9a9a9a"
            }

            ListView {
                id: styleList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: stylesVM.styleNames
                currentIndex: stylesVM.currentIndex
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: styleList.width
                    height: 28
                    color: index === stylesVM.currentIndex
                           ? Qt.rgba(Material.color(Material.Blue).r,
                                     Material.color(Material.Blue).g,
                                     Material.color(Material.Blue).b, 0.35)
                           : (index % 2 === 0 ? "#1f1f1f" : "#242424")

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        text: modelData
                        font.pixelSize: 12
                        color: "#dddddd"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: stylesVM.currentIndex = index
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                // Material's default vertical padding does not fit in a 30px
                // button, which makes the labels disappear.
                Button {
                    text: i18n.t("New")
                    implicitHeight: 30
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8
                    font.pixelSize: 12
                    onClicked: stylesVM.newStyle()
                }
                Button {
                    text: i18n.t("Copy")
                    implicitHeight: 30
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8
                    font.pixelSize: 12
                    enabled: stylesVM.currentIndex >= 0
                    onClicked: stylesVM.copyStyle(stylesVM.currentIndex)
                }
                Button {
                    text: i18n.t("Delete")
                    implicitHeight: 30
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 8
                    font.pixelSize: 12
                    enabled: stylesVM.currentIndex >= 0
                    onClicked: stylesVM.deleteStyle(stylesVM.currentIndex)
                }
            }

            Label {
                Layout.fillWidth: true
                text: stylesVM.lastError
                font.pixelSize: 11
                color: "#ff9a9a"
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }
        }

        // ---- Right: form + preview --------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            ScrollView {
                id: formScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: formScroll.availableWidth
                    spacing: 6
                    enabled: stylesVM.currentIndex >= 0

                    // Name + font -------------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Name"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }
                        TextField {
                            id: nameField
                            Layout.fillWidth: true
                            implicitHeight: stylesWindow.fieldHeight
                            font.pixelSize: 12
                            onEditingFinished: stylesVM.name = text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Font"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }
                        TextField {
                            id: fontField
                            Layout.fillWidth: true
                            implicitHeight: stylesWindow.fieldHeight
                            font.pixelSize: 12
                            onEditingFinished: stylesVM.fontName = text
                        }
                        Label { text: i18n.t("Size"); font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: fontSizeSpin
                            from: 1
                            to: 500
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 100
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && Math.round(stylesVM.fontSize) !== value)
                                    stylesVM.fontSize = value
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        CheckBox {
                            id: boldCheck
                            text: i18n.t("Bold")
                            font.pixelSize: 12
                            implicitHeight: stylesWindow.fieldHeight
                            topPadding: 0
                            bottomPadding: 0
                            onToggled: if (!stylesWindow.loadingForm && stylesVM.bold !== checked) stylesVM.bold = checked
                        }
                        CheckBox {
                            id: italicCheck
                            text: i18n.t("Italic")
                            font.pixelSize: 12
                            implicitHeight: stylesWindow.fieldHeight
                            topPadding: 0
                            bottomPadding: 0
                            onToggled: if (!stylesWindow.loadingForm && stylesVM.italic !== checked) stylesVM.italic = checked
                        }
                        CheckBox {
                            id: underlineCheck
                            text: i18n.t("Underline")
                            font.pixelSize: 12
                            implicitHeight: stylesWindow.fieldHeight
                            topPadding: 0
                            bottomPadding: 0
                            onToggled: if (!stylesWindow.loadingForm && stylesVM.underline !== checked) stylesVM.underline = checked
                        }
                        CheckBox {
                            id: strikeoutCheck
                            text: i18n.t("Strikeout")
                            font.pixelSize: 12
                            implicitHeight: stylesWindow.fieldHeight
                            topPadding: 0
                            bottomPadding: 0
                            onToggled: if (!stylesWindow.loadingForm && stylesVM.strikeout !== checked) stylesVM.strikeout = checked
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // Colors ------------------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Primary"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }
                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 24
                            radius: 3
                            border.color: "#555555"
                            color: stylesWindow.safeColor(primaryHex.text)
                        }
                        TextField {
                            id: primaryHex
                            Layout.preferredWidth: 100
                            implicitHeight: stylesWindow.fieldHeight
                            font.family: "Consolas"
                            font.pixelSize: 12
                            onEditingFinished: stylesVM.primaryColor = text
                        }
                        Label { text: "A"; font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: primaryAlphaSpin
                            from: 0
                            to: 255
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.primaryAlpha !== value)
                                    stylesVM.primaryAlpha = value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Secondary"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }
                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 24
                            radius: 3
                            border.color: "#555555"
                            color: stylesWindow.safeColor(secondaryHex.text)
                        }
                        TextField {
                            id: secondaryHex
                            Layout.preferredWidth: 100
                            implicitHeight: stylesWindow.fieldHeight
                            font.family: "Consolas"
                            font.pixelSize: 12
                            onEditingFinished: stylesVM.secondaryColor = text
                        }
                        Label { text: "A"; font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: secondaryAlphaSpin
                            from: 0
                            to: 255
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.secondaryAlpha !== value)
                                    stylesVM.secondaryAlpha = value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Outline"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }
                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 24
                            radius: 3
                            border.color: "#555555"
                            color: stylesWindow.safeColor(outlineHex.text)
                        }
                        TextField {
                            id: outlineHex
                            Layout.preferredWidth: 100
                            implicitHeight: stylesWindow.fieldHeight
                            font.family: "Consolas"
                            font.pixelSize: 12
                            onEditingFinished: stylesVM.outlineColor = text
                        }
                        Label { text: "A"; font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: outlineAlphaSpin
                            from: 0
                            to: 255
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.outlineAlpha !== value)
                                    stylesVM.outlineAlpha = value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Shadow"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }
                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 24
                            radius: 3
                            border.color: "#555555"
                            color: stylesWindow.safeColor(shadowHex.text)
                        }
                        TextField {
                            id: shadowHex
                            Layout.preferredWidth: 100
                            implicitHeight: stylesWindow.fieldHeight
                            font.family: "Consolas"
                            font.pixelSize: 12
                            onEditingFinished: stylesVM.shadowColor = text
                        }
                        Label { text: "A"; font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: shadowAlphaSpin
                            from: 0
                            to: 255
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.shadowAlpha !== value)
                                    stylesVM.shadowAlpha = value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // Alignment + outline/shadow/border ---------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            spacing: 4

                            Label { text: i18n.t("Alignment"); font.pixelSize: 11; color: "#9a9a9a" }

                            GridLayout {
                                columns: 3
                                rowSpacing: 2
                                columnSpacing: 2

                                Repeater {
                                    id: alignmentRepeater
                                    property int alignment: stylesVM.alignment
                                    model: 9

                                    delegate: Rectangle {
                                        implicitWidth: 34
                                        implicitHeight: 26
                                        radius: 3
                                        border.color: "#4a4a4a"
                                        color: stylesVM.alignment === index + 1
                                               ? Qt.rgba(Material.color(Material.Blue).r,
                                                         Material.color(Material.Blue).g,
                                                         Material.color(Material.Blue).b, 0.85)
                                               : "#2a2a2a"

                                        Label {
                                            anchors.centerIn: parent
                                            text: index + 1
                                            font.pixelSize: 11
                                            color: "#e6e6e6"
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: stylesVM.alignment = index + 1
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 6

                            RowLayout {
                                spacing: 6
                                Label { text: i18n.t("Border"); font.pixelSize: 11; color: "#9a9a9a" }
                                ComboBox {
                                    id: borderCombo
                                    implicitWidth: 170
                                    implicitHeight: stylesWindow.fieldHeight
                                    font.pixelSize: 12
                                    model: [i18n.t("Outline + Shadow"), i18n.t("Opaque Box")]
                                    onActivated: stylesVM.borderStyle = currentIndex === 1 ? 3 : 1
                                }
                            }

                            RowLayout {
                                spacing: 6
                                Label { text: i18n.t("Outline w."); font.pixelSize: 11; color: "#9a9a9a" }
                                SpinBox {
                                    id: outlineSpin
                                    from: 0
                                    to: 50
                                    editable: true
                                    implicitHeight: stylesWindow.fieldHeight
                                    implicitWidth: 96
                                    font.pixelSize: 12
                                    onValueChanged: {
                                        if (!stylesWindow.loadingForm && Math.round(stylesVM.outlineWidth) !== value)
                                            stylesVM.outlineWidth = value
                                    }
                                }
                                Label { text: i18n.t("Shadow d."); font.pixelSize: 11; color: "#9a9a9a" }
                                SpinBox {
                                    id: shadowSpin
                                    from: 0
                                    to: 50
                                    editable: true
                                    implicitHeight: stylesWindow.fieldHeight
                                    implicitWidth: 96
                                    font.pixelSize: 12
                                    onValueChanged: {
                                        if (!stylesWindow.loadingForm && Math.round(stylesVM.shadowDepth) !== value)
                                            stylesVM.shadowDepth = value
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Margins -----------------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: i18n.t("Margins"); Layout.preferredWidth: 74; font.pixelSize: 11; color: "#9a9a9a" }

                        Label { text: i18n.t("L"); font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: marginLSpin
                            from: 0
                            to: 10000
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.marginL !== value)
                                    stylesVM.marginL = value
                            }
                        }
                        Label { text: i18n.t("R"); font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: marginRSpin
                            from: 0
                            to: 10000
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.marginR !== value)
                                    stylesVM.marginR = value
                            }
                        }
                        Label { text: i18n.t("V"); font.pixelSize: 11; color: "#9a9a9a" }
                        SpinBox {
                            id: marginVSpin
                            from: 0
                            to: 10000
                            editable: true
                            implicitHeight: stylesWindow.fieldHeight
                            implicitWidth: 96
                            font.pixelSize: 12
                            onValueChanged: {
                                if (!stylesWindow.loadingForm && stylesVM.marginV !== value)
                                    stylesVM.marginV = value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- Live preview ---------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 68
                radius: 4
                color: "#141414"
                border.color: "#333333"

                Text {
                    anchors.centerIn: parent
                    text: i18n.t("Preview Text")
                    font.family: stylesVM.fontName
                    font.pixelSize: Math.max(10, Math.min(48, stylesVM.fontSize / 2))
                    font.bold: stylesVM.bold
                    font.italic: stylesVM.italic
                    font.underline: stylesVM.underline
                    font.strikeout: stylesVM.strikeout
                    color: stylesWindow.safeColor(stylesVM.primaryColor)
                    style: Text.Outline
                    styleColor: stylesWindow.safeColor(stylesVM.outlineColor)
                }
            }
        }
    }

    Connections {
        target: stylesVM
        function onReloadForm() { stylesWindow.loadStyleForm() }
    }

    Component.onCompleted: loadStyleForm()
}
