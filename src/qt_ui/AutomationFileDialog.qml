import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Dialogs.FileDialog {
    id: picker
    objectName: "automationFileDialog"

    property bool requestPending: false
    property bool collectPaths: false
    property var collected: []

    function pathUrl(value, base) {
        const text = String(value || "")
        if (!text || /^file:/i.test(text))
            return text
        const path = text.replace(/\\/g, "/")
        const escaped = path.split("/").map(part => encodeURIComponent(part)).join("/")
        if (/^[a-z]:\//i.test(path))
            return "file:///" + path.slice(0, 2) + escaped.slice(4)
        if (path.startsWith("//"))
            return "file:" + escaped
        if (path.startsWith("/"))
            return "file://" + escaped
        const folder = String(base || "")
        return folder ? folder.replace(/\/$/, "") + "/" + escaped : escaped
    }

    function start(request) {
        if (!automationVM.busy || automationVM.cancelling)
            return
        requestPending = true
        const canSelectMissing = !request.save && request.mustExist === false
        collectPaths = canSelectMissing && request.multiple
        collected = []
        title = request.title || i18n.t("Select File")
        nameFilters = request.filters && request.filters.length ? request.filters : [i18n.t("All Files") + " (*)"]
        if (request.save || canSelectMissing) {
            // Qt requires SaveFile mode to accept a path that does not exist.
            fileMode = Dialogs.FileDialog.SaveFile
            options = canSelectMissing || request.dontPrompt ? Dialogs.FileDialog.DontConfirmOverwrite : 0
        } else {
            fileMode = request.multiple ? Dialogs.FileDialog.OpenFiles : Dialogs.FileDialog.OpenFile
            options = 0
        }
        acceptLabel = request.save ? i18n.t("Save") : canSelectMissing ? i18n.t("Select") : i18n.t("Open")
        if (request.directory)
            currentFolder = pathUrl(request.directory, currentFolder)
        if (request.file)
            selectedFile = pathUrl(request.file, currentFolder)
        open()
    }

    function dismissRequest() {
        if (!requestPending)
            return
        requestPending = false
        collection.close()
        close()
        collected = []
    }

    function finish(files) {
        if (!requestPending)
            return
        requestPending = false
        automationVM.completeFile(files)
    }

    onAccepted: {
        if (!requestPending)
            return
        if (!collectPaths) {
            finish(selectedFiles)
            return
        }
        collected = collected.concat(Array.from(selectedFiles))
        collection.open()
    }
    onRejected: finish([])

    property Dialog collection: Dialog {
        id: collectionDialog
        objectName: "automationFileCollection"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: picker.title
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: Math.min(parent ? parent.width - 32 : 480, 480)
        onAccepted: picker.finish(picker.collected)
        onRejected: picker.finish([])
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: picker.collected.map(path => String(path)).join("\n")
                wrapMode: Text.WrapAnywhere
            }
            Button {
                text: i18n.t("Add File...")
                onClicked: {
                    collection.close()
                    picker.open()
                }
            }
        }
    }

    // FileDialog itself is not an Item and has no default child property.
    property Connections requests: Connections {
        target: automationVM
        function onFileRequested(request) { picker.start(request) }
        function onOperationChanged() {
            if (automationVM.cancelling || !automationVM.busy)
                picker.dismissRequest()
        }
    }
}
