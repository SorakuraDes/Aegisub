import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Aegisub 1.0

// Edit box for the selected line: two compact toolbars (metadata + times) and
// the body text editor. Fields are filled by loadSelection() instead of being
// bound, so typing never fights a binding and external changes (undo, row
// switch) can be pushed in explicitly.
Rectangle {
    id: editBox
    color: themeVM.surfaceContainerLow
    radius: themeVM.radiusMedium

    AssSyntaxHighlighter {
        target: textArea.textDocument
        primary: themeVM.primary
        darkMode: themeVM.darkMode
    }
    implicitHeight: 196

    property bool editorReady: false
    readonly property int fieldHeight: themeVM.controlHeight
    readonly property int labelSize: 11

    // Rewrites the time fields from the view model. While typing, only the
    // fields the caret is NOT in are refreshed (the view model normalizes what
    // it parses, which would move the caret); `force` is used for external
    // changes (undo/redo, row switch), where the stale text must go away even
    // if the field happens to have focus.
    function syncTimeFields(force) {
        if ((force || !startField.activeFocus) && startField.text !== editBoxVM.startTime)
            startField.text = editBoxVM.startTime
        if ((force || !endField.activeFocus) && endField.text !== editBoxVM.endTime)
            endField.text = editBoxVM.endTime
        if ((force || !durationField.activeFocus) && durationField.text !== editBoxVM.duration)
            durationField.text = editBoxVM.duration
    }

    function loadSelection() {
        // External change: every field is rewritten, including the focused one.
        syncTimeFields(true)

        if (commentCheck.checked !== editBoxVM.comment)
            commentCheck.checked = editBoxVM.comment
        if (layerSpin.value !== editBoxVM.layer)
            layerSpin.value = editBoxVM.layer
        if (actorField.text !== editBoxVM.actor)
            actorField.text = editBoxVM.actor
        if (effectField.text !== editBoxVM.effect)
            effectField.text = editBoxVM.effect

        var styleIndex = editBoxVM.availableStyles.indexOf(editBoxVM.style)
        if (styleIndex >= 0 && styleCombo.currentIndex !== styleIndex)
            styleCombo.currentIndex = styleIndex

        if (textArea.text !== editBoxVM.text)
            textArea.text = editBoxVM.text
    }

    // Shift+Enter: insert a literal ASS hard line break ("\N" in the file).
    function insertHardLineBreak() {
        var pos = textArea.cursorPosition
        var before = textArea.text.substring(0, pos)
        var after = textArea.text.substring(pos)
        textArea.text = before + "\\N" + after
        textArea.cursorPosition = pos + 2
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        // ---- Row 1: metadata ------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            CheckBox {
                id: commentCheck
                text: i18n.t("Comment")
                font.pixelSize: 12
                implicitHeight: editBox.fieldHeight
                topPadding: 0
                bottomPadding: 0
                enabled: gridModel.activeRow >= 0
                // Lets Main.qml keep Delete/Insert/Ctrl+D away from text editing.
                property bool isEditBoxField: true
                onToggled: if (editBoxVM.comment !== checked) editBoxVM.comment = checked
            }

            Label {
                text: i18n.t("Layer")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            SpinBox {
                id: layerSpin
                from: 0
                to: 9999
                editable: true
                implicitHeight: editBox.fieldHeight
                implicitWidth: 84
                font.pixelSize: 12
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onValueChanged: if (editBoxVM.layer !== value) editBoxVM.layer = value
            }

            Label {
                text: i18n.t("Style")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            ComboBox {
                id: styleCombo
                model: editBoxVM.availableStyles
                implicitHeight: editBox.fieldHeight
                implicitWidth: 118
                font.pixelSize: 12
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onActivated: editBoxVM.style = currentText
            }

            Label {
                text: i18n.t("Actor")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            TextField {
                id: actorField
                Layout.preferredWidth: 110
                implicitHeight: editBox.fieldHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 8
                rightPadding: 8
                font.pixelSize: 12
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onTextEdited: editBoxVM.actor = text
            }

            Label {
                text: i18n.t("Effect")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            TextField {
                id: effectField
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                implicitHeight: editBox.fieldHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 8
                rightPadding: 8
                font.pixelSize: 12
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onTextEdited: editBoxVM.effect = text
            }

            Label {
                text: gridModel.activeRow >= 0
                      ? "#" + (gridModel.activeRow + 1)
                      : i18n.t("No line selected")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            Label {
                text: textArea.text.length + " " + i18n.t("chars")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }
        }

        // ---- Row 2: times ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: i18n.t("Start")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            TextField {
                id: startField
                Layout.preferredWidth: 118
                implicitHeight: editBox.fieldHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 8
                rightPadding: 8
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onTextEdited: editBoxVM.startTime = text
            }

            Label {
                text: i18n.t("End")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            TextField {
                id: endField
                Layout.preferredWidth: 118
                implicitHeight: editBox.fieldHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 8
                rightPadding: 8
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onTextEdited: editBoxVM.endTime = text
            }

            Label {
                text: i18n.t("Duration")
                font.pixelSize: editBox.labelSize
                color: themeVM.onSurfaceVariant
            }

            TextField {
                id: durationField
                Layout.preferredWidth: 118
                implicitHeight: editBox.fieldHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 8
                rightPadding: 8
                font.family: "Consolas"
                font.pixelSize: 13
                enabled: gridModel.activeRow >= 0
                property bool isEditBoxField: true
                onTextEdited: editBoxVM.duration = text
            }

            Item { Layout.fillWidth: true }
        }

        // ---- Body: the panel lives on the ScrollView so the TextArea can keep
        // its own (zero-inset) geometry: text then always stays inside the
        // background. ----------------------------------------------
        ScrollView {
            id: textScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 56
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            background: Rectangle {
                color: themeVM.surfaceContainerHighest
                radius: themeVM.radiusSmall
            }

            TextArea {
                id: textArea
                // Read by main.cpp's key filter: the TextArea swallows Enter
                // itself, so Enter/Shift+Enter are forwarded from C++.
                objectName: "editBoxTextArea"
                width: textScroll.availableWidth
                height: implicitHeight
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                leftPadding: 10
                rightPadding: 10
                topPadding: 8
                bottomPadding: 8
                font.pixelSize: 14
                textFormat: TextEdit.PlainText
                color: themeVM.onSurface
                wrapMode: TextArea.Wrap
                enabled: gridModel.activeRow >= 0
                // Lets Main.qml keep Delete/Insert/Ctrl+D away from text editing.
                property bool isEditBoxField: true
                placeholderText: i18n.t("No line selected")
                onTextChanged: if (editBox.editorReady && editBoxVM.text !== text) editBoxVM.text = text
                background: null
            }
        }
    }

    // Reload the editors when the selection changes or the model is reset
    // (new/open document). Editing the fields must not fight the user's
    // cursor, so we do not bind them to the view model properties.
    Connections {
        target: gridModel
        function onActiveRowChanged() { loadSelection() }
        function onModelReset() { loadSelection() }
    }

    // External changes (undo/redo, another editor's commit) refill every field;
    // time edits made here refresh only the derived fields, never the one the
    // caret is in.
    Connections {
        target: editBoxVM
        function onExternalReload() { loadSelection() }
        function onStartTimeChanged() { syncTimeFields() }
        function onEndTimeChanged() { syncTimeFields() }
        function onDurationChanged() { syncTimeFields() }
        function onAvailableStylesChanged() { loadSelection() }
        function onLineBreakRequested() { insertHardLineBreak() }
        function onBodyEditorFocusRequested() {
            textArea.forceActiveFocus()
            textArea.selectAll()
        }
    }

    Component.onCompleted: {
        loadSelection()
        editorReady = true
    }
}
