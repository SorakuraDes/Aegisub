import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

GridLayout {
    id: form
    objectName: "luaForm"
    property var controls: []
    rowSpacing: 4
    columnSpacing: 8

    function values() {
        const result = Object.create(null)
        for (let i = 0; i < fields.count; ++i) {
            const field = fields.itemAt(i) as Loader
            if (field && field.item && typeof field.item.readValue === "function")
                result[field.spec.name || ""] = field.item.readValue()
        }
        return result
    }

    function pickerColor(value) {
        const match = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(value)
        const bytes = match ? match[1] : "000000"
        return Qt.rgba(parseInt(bytes.slice(0, 2), 16) / 255,
                       parseInt(bytes.slice(2, 4), 16) / 255,
                       parseInt(bytes.slice(4, 6), 16) / 255,
                       match && match[2] ? 1 - parseInt(match[2], 16) / 255 : 1)
    }

    function hexByte(fraction) {
        return ("0" + Math.round(fraction * 255).toString(16)).slice(-2).toUpperCase()
    }

    function luaColor(c, alpha) {
        // ASS alpha is transparency, not opacity; Lua expects it last.
        return "#" + hexByte(c.r) + hexByte(c.g) + hexByte(c.b)
                + (alpha ? hexByte(1 - c.a) : "")
    }

    Repeater {
        id: fields
        model: form.controls
        delegate: Loader {
            id: field
            required property var modelData
            readonly property var spec: modelData
            readonly property string kind: spec["class"]
            readonly property string hook: "luaControl_" + (spec.name || "")
            readonly property string hint: spec.hint || ""
            readonly property string initialText: spec.value == null ? "" : String(spec.value)
            Layout.column: Math.max(0, spec.x || 0)
            Layout.row: Math.max(0, spec.y || 0)
            Layout.columnSpan: Math.max(1, spec.width || 1)
            Layout.rowSpan: Math.max(1, spec.height || 1)
            Layout.fillWidth: kind !== "label"
            Layout.fillHeight: kind === "textbox"
            sourceComponent: {
                switch (kind) {
                case "label": return labelField
                case "edit": case "alpha": return editField
                case "textbox": return textboxField
                case "intedit": return integerField
                case "floatedit": return floatField
                case "dropdown": return dropdownField
                case "checkbox": return checkboxField
                case "color": case "coloralpha": return colorField
                default: return null
                }
            }

            Component {
                id: labelField
                Label {
                    objectName: field.hook
                    text: field.spec.label || ""
                    textFormat: Text.PlainText
                    HoverHandler { id: labelHover }
                    ToolTip.text: field.hint
                    ToolTip.visible: labelHover.hovered && field.hint !== ""
                    ToolTip.delay: 500
                }
            }

            Component {
                id: editField
                TextField {
                    objectName: field.hook
                    implicitWidth: 170
                    text: field.initialText
                    selectByMouse: true
                    Accessible.name: field.spec.label || field.spec.name || ""
                    ToolTip.text: field.hint
                    ToolTip.visible: hovered && field.hint !== ""
                    ToolTip.delay: 500
                    function readValue() { return text }
                }
            }

            Component {
                id: textboxField
                ScrollView {
                    implicitWidth: 240
                    implicitHeight: Math.min(240, Math.max(76, field.Layout.rowSpan * 34))
                    contentWidth: availableWidth
                    clip: true
                    function readValue() { return editor.text }
                    TextArea {
                        id: editor
                        objectName: field.hook
                        text: field.initialText
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        Accessible.name: field.spec.label || field.spec.name || ""
                        ToolTip.text: field.hint
                        ToolTip.visible: hovered && field.hint !== ""
                        ToolTip.delay: 500
                    }
                }
            }

            Component {
                id: integerField
                SpinBox {
                    objectName: field.hook
                    editable: true
                    from: field.spec.min == null ? -2147483648 : field.spec.min
                    to: field.spec.max == null ? 2147483647 : field.spec.max
                    value: Number(field.spec.value) || 0
                    stepSize: Math.max(1, Number(field.spec.step) || 1)
                    Accessible.name: field.spec.label || field.spec.name || ""
                    ToolTip.text: field.hint
                    ToolTip.visible: hovered && field.hint !== ""
                    ToolTip.delay: 500
                    function readValue() {
                        const input = contentItem as TextInput
                        if (input && input.acceptableInput && input.text.trim() !== "") {
                            const pending = valueFromText(input.text, locale)
                            if (Number.isFinite(pending) && pending >= from && pending <= to)
                                return pending
                        }
                        return value
                    }
                }
            }

            Component {
                id: floatField
                RowLayout {
                    id: floatEditor
                    readonly property real lower: field.spec.min == null ? -Number.MAX_VALUE : field.spec.min
                    readonly property real upper: field.spec.max == null ? Number.MAX_VALUE : field.spec.max
                    readonly property real increment: Math.max(0, Number(field.spec.step) || 0)
                    readonly property var numericLocale: Qt.locale()
                    property real lastValid: Math.max(lower, Math.min(upper, Number(field.spec.value) || 0))
                    spacing: 0

                    function readValue() {
                        if (!numberInput.acceptableInput || numberInput.text.trim() === "")
                            return lastValid
                        const parsed = Number.fromLocaleString(numericLocale, numberInput.text)
                        return Number.isFinite(parsed) ? parsed : lastValid
                    }
                    function stepBy(direction) {
                        lastValid = Math.max(lower, Math.min(upper, readValue() + increment * direction))
                        numberInput.text = formatValue(lastValid)
                    }
                    function formatValue(value) {
                        return String(value).replace(".", numericLocale.decimalPoint)
                    }

                    TextField {
                        id: numberInput
                        objectName: field.hook
                        Layout.fillWidth: true
                        implicitWidth: 120
                        text: floatEditor.formatValue(floatEditor.lastValid)
                        selectByMouse: true
                        validator: DoubleValidator { bottom: floatEditor.lower; top: floatEditor.upper }
                        Accessible.name: field.spec.label || field.spec.name || ""
                        ToolTip.text: field.hint
                        ToolTip.visible: hovered && field.hint !== ""
                        ToolTip.delay: 500
                        onEditingFinished: floatEditor.lastValid = floatEditor.readValue()
                        Keys.onUpPressed: event => {
                            event.accepted = floatEditor.increment > 0
                            if (event.accepted) floatEditor.stepBy(1)
                        }
                        Keys.onDownPressed: event => {
                            event.accepted = floatEditor.increment > 0
                            if (event.accepted) floatEditor.stepBy(-1)
                        }
                    }
                    ToolButton {
                        visible: floatEditor.increment > 0
                        text: "-"
                        Accessible.name: qsTr("Decrease")
                        autoRepeat: true
                        onClicked: floatEditor.stepBy(-1)
                    }
                    ToolButton {
                        visible: floatEditor.increment > 0
                        text: "+"
                        Accessible.name: qsTr("Increase")
                        autoRepeat: true
                        onClicked: floatEditor.stepBy(1)
                    }
                }
            }

            Component {
                id: dropdownField
                ComboBox {
                    objectName: field.hook
                    model: field.spec.items || []
                    currentIndex: {
                        const choices = field.spec.items || []
                        for (let i = 0; i < choices.length; ++i)
                            if (choices[i] === field.initialText)
                                return i
                        return -1
                    }
                    Accessible.name: field.spec.label || field.spec.name || ""
                    ToolTip.text: field.hint
                    ToolTip.visible: hovered && field.hint !== ""
                    ToolTip.delay: 500
                    function readValue() { return currentIndex >= 0 ? currentText : field.initialText }
                }
            }

            Component {
                id: checkboxField
                CheckBox {
                    objectName: field.hook
                    text: field.spec.label || ""
                    checked: !!field.spec.value
                    ToolTip.text: field.hint
                    ToolTip.visible: hovered && field.hint !== ""
                    ToolTip.delay: 500
                    function readValue() { return checked }
                }
            }

            Component {
                id: colorField
                Button {
                    id: colorButton
                    objectName: field.hook
                    readonly property bool showAlpha: field.kind === "coloralpha"
                    property color selectedColor: form.pickerColor(field.initialText)
                    Accessible.name: field.spec.label || field.spec.name || ""
                    ToolTip.text: field.hint || readValue()
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    function readValue() { return form.luaColor(selectedColor, showAlpha) }
                    contentItem: Rectangle {
                        implicitWidth: 54
                        implicitHeight: 18
                        radius: 3
                        color: Qt.rgba(colorButton.selectedColor.r, colorButton.selectedColor.g,
                                       colorButton.selectedColor.b, 1)
                        border.color: Material.foreground
                    }
                    onClicked: {
                        colorPicker.selectedColor = selectedColor
                        colorPicker.open()
                    }
                    Dialogs.ColorDialog {
                        id: colorPicker
                        objectName: field.hook + "Picker"
                        options: colorButton.showAlpha ? Dialogs.ColorDialog.ShowAlphaChannel : 0
                        onAccepted: colorButton.selectedColor = selectedColor
                    }
                }
            }
        }
    }
}