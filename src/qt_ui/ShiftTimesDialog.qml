import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Non-modal Shift Times window (Subtitle -> Shift Times / Ctrl+I).
//
// Pure view: every decision (parsing the time, frame <-> time mapping, which
// rows are affected, the single commit) lives in ShiftTimesViewModel. The form
// is populated explicitly in loadForm() instead of two-way bindings, so the
// controls' initial values cannot overwrite the view model (see
// docs/qt6-frontend.md #26).
Window {
    id: shiftWindow
    width: 420
    height: 470
    minimumWidth: 380
    minimumHeight: 430
    title: "Shift Times"
    color: "#1e1e1e"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    readonly property int fieldHeight: 32

    // True while loadForm() assigns the fields. Without it the SpinBox's
    // initial value fires onValueChanged and writes 0 back into the model.
    property bool loadingForm: true

    function loadForm() {
        loadingForm = true
        shiftTimeField.text = shiftTimesVM.shiftTime
        shiftFramesSpin.value = shiftTimesVM.shiftFrames
        loadingForm = false
    }

    // Keep the Time / Frames radios in sync when the view model changes the
    // mode itself (e.g. it drops back to time mode when the video is closed).
    // Clicking a radio breaks the `checked` binding, hence the explicit sync.
    function syncModeRadios() {
        byTimeRadio.checked = !shiftTimesVM.byFrames
        byFramesRadio.checked = shiftTimesVM.byFrames
    }

    Component.onCompleted: loadForm()

    Connections {
        target: shiftTimesVM
        function onByFramesChanged() { shiftWindow.syncModeRadios() }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: shiftWindow.close()
    }

    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        onActivated: shiftTimesVM.applyShift()
    }

    ButtonGroup { id: modeGroup }
    ButtonGroup { id: directionGroup }
    ButtonGroup { id: affectGroup }
    ButtonGroup { id: targetGroup }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        // ---- Shift by -------------------------------------------------------
        Label {
            text: i18n.t("Shift by")
            font.pixelSize: 11
            color: "#9a9a9a"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            RadioButton {
                id: byTimeRadio
                text: i18n.t("Time")
                font.pixelSize: 12
                topPadding: 0
                bottomPadding: 0
                Layout.alignment: Qt.AlignVCenter
                ButtonGroup.group: modeGroup
                checked: !shiftTimesVM.byFrames
                onToggled: if (checked) shiftTimesVM.byFrames = false
            }

            TextField {
                id: shiftTimeField
                objectName: "shiftTimeField"
                Layout.preferredWidth: 140
                implicitHeight: shiftWindow.fieldHeight
                font.pixelSize: 13
                enabled: !shiftTimesVM.byFrames
                // textEdited only fires for real key presses, which would make
                // the field impervious to programmatic/automation input; the
                // guard keeps loadForm() from writing the value back.
                onTextChanged: {
                    if (shiftTimesVM.shiftTime !== text)
                        shiftTimesVM.shiftTime = text
                }
                onAccepted: shiftTimesVM.applyShift()
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            RadioButton {
                id: byFramesRadio
                text: i18n.t("Frames")
                font.pixelSize: 12
                topPadding: 0
                bottomPadding: 0
                Layout.alignment: Qt.AlignVCenter
                enabled: shiftTimesVM.framesAvailable
                ButtonGroup.group: modeGroup
                checked: shiftTimesVM.byFrames
                onToggled: if (checked) shiftTimesVM.byFrames = true
            }

            SpinBox {
                id: shiftFramesSpin
                from: -100000
                to: 100000
                editable: true
                enabled: shiftTimesVM.byFrames
                Layout.preferredWidth: 140
                implicitHeight: shiftWindow.fieldHeight
                font.pixelSize: 13
                onValueChanged: {
                    if (!shiftWindow.loadingForm && shiftTimesVM.shiftFrames !== value)
                        shiftTimesVM.shiftFrames = value
                }
            }

            Item { Layout.fillWidth: true }
        }

        // ---- Direction ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 20

            RadioButton {
                text: i18n.t("Forward")
                font.pixelSize: 12
                topPadding: 0
                bottomPadding: 0
                ButtonGroup.group: directionGroup
                checked: !shiftTimesVM.backward
                onToggled: if (checked) shiftTimesVM.backward = false
            }

            RadioButton {
                text: i18n.t("Backward")
                font.pixelSize: 12
                topPadding: 0
                bottomPadding: 0
                ButtonGroup.group: directionGroup
                checked: shiftTimesVM.backward
                onToggled: if (checked) shiftTimesVM.backward = true
            }

            Item { Layout.fillWidth: true }
        }

        // ---- Affect ---------------------------------------------------------
        Label {
            text: i18n.t("Affect")
            font.pixelSize: 11
            color: "#9a9a9a"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            RadioButton {
                text: i18n.t("All rows")
                font.pixelSize: 11
                topPadding: 0
                bottomPadding: 0
                spacing: 4
                ButtonGroup.group: affectGroup
                checked: shiftTimesVM.affectScope === 0
                onToggled: if (checked) shiftTimesVM.affectScope = 0
            }

            RadioButton {
                text: i18n.t("Selected rows")
                font.pixelSize: 11
                topPadding: 0
                bottomPadding: 0
                spacing: 4
                ButtonGroup.group: affectGroup
                checked: shiftTimesVM.affectScope === 1
                onToggled: if (checked) shiftTimesVM.affectScope = 1
            }

            RadioButton {
                text: i18n.t("Selection onward")
                font.pixelSize: 11
                topPadding: 0
                bottomPadding: 0
                spacing: 4
                ButtonGroup.group: affectGroup
                checked: shiftTimesVM.affectScope === 2
                onToggled: if (checked) shiftTimesVM.affectScope = 2
            }

            Item { Layout.fillWidth: true }
        }

        // ---- Time fields ----------------------------------------------------
        Label {
            text: i18n.t("Time fields")
            font.pixelSize: 11
            color: "#9a9a9a"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            RadioButton {
                text: i18n.t("Start and End")
                font.pixelSize: 11
                topPadding: 0
                bottomPadding: 0
                spacing: 4
                ButtonGroup.group: targetGroup
                checked: shiftTimesVM.timeTarget === 0
                onToggled: if (checked) shiftTimesVM.timeTarget = 0
            }

            RadioButton {
                text: i18n.t("Start times only")
                font.pixelSize: 11
                topPadding: 0
                bottomPadding: 0
                spacing: 4
                ButtonGroup.group: targetGroup
                checked: shiftTimesVM.timeTarget === 1
                onToggled: if (checked) shiftTimesVM.timeTarget = 1
            }

            RadioButton {
                text: i18n.t("End times only")
                font.pixelSize: 11
                topPadding: 0
                bottomPadding: 0
                spacing: 4
                ButtonGroup.group: targetGroup
                checked: shiftTimesVM.timeTarget === 2
                onToggled: if (checked) shiftTimesVM.timeTarget = 2
            }

            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }

        // Hidden while empty so it does not take up a blank row.
        Label {
            id: statusLabel
            Layout.fillWidth: true
            text: shiftTimesVM.statusMessage
            visible: text.length > 0
            font.pixelSize: 11
            color: "#8fd18f"
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                objectName: "shiftTimesApplyButton"
                text: i18n.t("Apply")
                highlighted: true
                onClicked: shiftTimesVM.applyShift()
            }

            Button {
                text: i18n.t("Close")
                onClicked: shiftWindow.close()
            }
        }
    }
}
