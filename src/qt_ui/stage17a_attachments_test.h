#pragma once

#include "viewmodels/attachments_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/undo_manager.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <QtTest/QTest>

namespace stage17a_attachments_test {
inline constexpr int timeout = 5000;

inline QObject *object(const QVariant &value) {
    if (auto *obj = value.value<QObject *>()) return obj;
    return value.value<QJSValue>().toQObject();
}

inline QObject *qmlObject(QObject *scope, const QString &id) {
    if (!qmlContext(scope)) return nullptr;
    QQmlExpression expression(qmlContext(scope), scope, id);
    auto *result = object(expression.evaluate());
    if (expression.hasError()) qWarning().noquote() << expression.error().toString();
    return result;
}

inline QQuickItem *visualItem(QQuickItem *item, const QString &name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *match = visualItem(child, name)) return match;
    return nullptr;
}

inline bool click(QQuickItem *item) {
    if (!item || !item->window() || !item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0)
        return false;
    const auto pos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, pos);
    return true;
}

inline int role(const QAbstractListModel *model, const QByteArray &name) {
    const auto roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name) return it.key();
    return -1;
}

inline int rowNamed(const QAbstractListModel *model, int nameRole, const QString &name) {
    for (int i = 0; i < model->rowCount(); ++i)
        if (model->data(model->index(i, 0), nameRole).toString() == name) return i;
    return -1;
}

inline bool writeBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size() && file.flush();
}

inline bool hasBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readAll() == bytes;
}
} // namespace stage17a_attachments_test

// The caller supplies the loaded Main.qml engine with real VMs and the application's undo shortcut route.
// Returns 0 on success or 1 with a diagnostic; leaves the clean round-trip document loaded.
inline int runStage17AAttachmentsTest(QQmlApplicationEngine &engine, const QString &scratchPath) {
    using namespace stage17a_attachments_test;
#define STAGE17A_ATTACH_CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 17A attachments:" << __LINE__ << message; return 1; \
} } while (false)

    auto *context = engine.rootContext();
    const auto contextObject = [&](const QString &name) { return object(context->contextProperty(name)); };
    auto *attachments = qobject_cast<AttachmentsViewModel *>(contextObject(QStringLiteral("attachmentsVM")));
    auto *document = qobject_cast<DocumentViewModel *>(contextObject(QStringLiteral("documentVM")));
    auto *undo = qobject_cast<UndoManager *>(contextObject(QStringLiteral("undoVM")));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    STAGE17A_ATTACH_CHECK(attachments && document && undo && window, "Missing real attachments/document/undo VMs or Main.qml window");

    auto *dialog = qmlObject(window, QStringLiteral("attachmentsDialog"));
    STAGE17A_ATTACH_CHECK(dialog, "Main.qml did not expose its attachmentsDialog");
    const auto closeDialog = [&] {
        return QMetaObject::invokeMethod(dialog, "close") &&
               QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, timeout);
    };
    const auto closeOnExit = qScopeGuard([&] {
        if (dialog->property("visible").toBool()) closeDialog();
    });
    const auto openDialog = [&] {
        return QMetaObject::invokeMethod(window, "openAttachments") &&
               QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, timeout);
    };
    const auto control = [&](const QString &name) {
        return visualItem(qobject_cast<QQuickItem *>(object(dialog->property("contentItem"))), name);
    };
    const auto viewCount = [&](int expected) {
        return QTest::qWaitFor([&] {
            auto *view = control(QStringLiteral("attachmentsList"));
            return view && object(view->property("model")) == attachments && view->property("count").toInt() == expected &&
                   attachments->count() == expected;
        }, timeout);
    };
    const auto selectRow = [&](int row) {
        if (row < 0) return false;
        const auto name = QStringLiteral("attachmentsSelect_%1").arg(row);
        return QTest::qWaitFor([&] { return control(name) != nullptr; }, timeout) && click(control(name)) &&
               QTest::qWaitFor([&] {
                   auto *delegate = control(name);
                   return delegate && delegate->property("checked").toBool();
               }, timeout);
    };
    const auto clearError = [&] {
        attachments->clearError();
        return attachments->lastError().isEmpty();
    };

    STAGE17A_ATTACH_CHECK(QDir(scratchPath).exists(), "The integration scratch directory must already exist");
    QTemporaryDir fixture(QDir(scratchPath).filePath(QStringLiteral("stage17a-\u9644\u4ef6-XXXXXX")));
    STAGE17A_ATTACH_CHECK(fixture.isValid(), fixture.errorString());
    const QDir dir(fixture.path());
    const auto fontName = QStringLiteral("\u5b57\u4f53.ttc");
    const auto graphicName = QStringLiteral("\u56fe\u7247.bmp");
    const auto fontPath = dir.filePath(fontName);
    const auto graphicPath = dir.filePath(graphicName);
    const auto assPath = dir.filePath(QStringLiteral("\u9644\u4ef6\u5b58\u6863.ass"));
    const auto singlePath = dir.filePath(QStringLiteral("\u5355\u4e2a\u63d0\u53d6.bin"));
    const auto batchName = QStringLiteral("\u6279\u91cf\u63d0\u53d6");
    const QDir batch(dir.filePath(batchName));
    const auto batchUrl = QUrl::fromLocalFile(batch.absolutePath());
    const QByteArray fontBytes = QByteArray::fromHex("74746366000d0aabff42696e617279");
    const QByteArray graphicBytes = QByteArray::fromHex("424d000d0a1a0a0067ff0a");
    STAGE17A_ATTACH_CHECK(dir.mkdir(batchName) && writeBytes(fontPath, fontBytes) && writeBytes(graphicPath, graphicBytes),
                         "Could not create the binary input fixtures and extraction directory");
    STAGE17A_ATTACH_CHECK(document->newDocument() && !document->dirty() && openDialog() && viewCount(0),
                         "New document did not display an empty attachments list");

    const int nameRole = role(attachments, "name");
    const int groupRole = role(attachments, "group");
    const int sizeRole = role(attachments, "size");
    STAGE17A_ATTACH_CHECK(nameRole >= 0 && groupRole >= 0 && sizeRole >= 0, "Missing name/group/size model roles");
    const auto fontRow = [&] { return rowNamed(attachments, nameRole, fontName); };
    const auto graphicRow = [&] { return rowNamed(attachments, nameRole, graphicName); };
    const auto rowData = [&](int row, int dataRole) { return attachments->data(attachments->index(row, 0), dataRole); };
    STAGE17A_ATTACH_CHECK(attachments->attachFonts({QUrl::fromLocalFile(fontPath)}) && attachments->lastError().isEmpty() &&
                         document->dirty() && viewCount(1) && fontRow() >= 0, "Could not attach Chinese font fixture:" << attachments->lastError());
    STAGE17A_ATTACH_CHECK(attachments->attachGraphics({QUrl::fromLocalFile(graphicPath)}) && attachments->lastError().isEmpty() &&
                         viewCount(2) && graphicRow() >= 0, "Could not attach Chinese graphic fixture:" << attachments->lastError());
    const auto fontGroup = rowData(fontRow(), groupRole);
    const auto graphicGroup = rowData(graphicRow(), groupRole);
    const auto encodedSize = [](qsizetype size) { return (size * 4 + 2) / 3; }; // No line wrapping for these short payloads.
    STAGE17A_ATTACH_CHECK(fontGroup.toString() == QStringLiteral("[Fonts]") && graphicGroup.toString() == QStringLiteral("[Graphics]") &&
                         rowData(fontRow(), sizeRole).toLongLong() == encodedSize(fontBytes.size()) &&
                         rowData(graphicRow(), sizeRole).toLongLong() == encodedSize(graphicBytes.size()),
                         "Model did not report ASS groups and encoded attachment sizes");
    const auto fontDefault = attachments->extractionFileUrl(fontRow());
    STAGE17A_ATTACH_CHECK(!fontDefault.isEmpty() && fontDefault.fileName() == fontName,
                         "Single extraction picker did not receive the attachment basename:" << fontDefault);
    STAGE17A_ATTACH_CHECK(attachments->extractAttachment(fontRow(), QUrl::fromLocalFile(singlePath)) && attachments->lastError().isEmpty() &&
                         hasBytes(singlePath, fontBytes), "Single-file extraction changed binary data:" << attachments->lastError());

    STAGE17A_ATTACH_CHECK(document->saveFileAs(QUrl::fromLocalFile(assPath)) && !document->dirty(), document->lastError());
    QFile ass(assPath);
    STAGE17A_ATTACH_CHECK(ass.open(QIODevice::ReadOnly), ass.errorString());
    const auto saved = ass.readAll();
    ass.close();
    STAGE17A_ATTACH_CHECK(saved.contains("[Fonts]") && saved.contains("fontname: " + fontName.toUtf8()) &&
                         saved.contains("[Graphics]") && saved.contains("filename: " + graphicName.toUtf8()),
                         "Saved ASS did not contain both correctly named attachment sections");
    STAGE17A_ATTACH_CHECK(closeDialog() && document->newDocument() && attachments->rowCount() == 0,
                         "New document retained old attachments");
    STAGE17A_ATTACH_CHECK(document->openFile(QUrl::fromLocalFile(assPath)) && !document->dirty() && openDialog() && viewCount(2) &&
                         fontRow() >= 0 && graphicRow() >= 0 && rowData(fontRow(), groupRole) == fontGroup &&
                         rowData(graphicRow(), groupRole) == graphicGroup,
                         "Reopened ASS did not repopulate the real dialog:" << document->lastError());
    const QVariantList both{fontRow(), graphicRow()};
    STAGE17A_ATTACH_CHECK(attachments->validateExtraction(both) && attachments->extractAttachments(both, batchUrl) && attachments->lastError().isEmpty() &&
                         hasBytes(batch.filePath(fontName), fontBytes) && hasBytes(batch.filePath(graphicName), graphicBytes) &&
                         !document->dirty(), "Batch extraction from the reopened ASS did not preserve both binary payloads:" << attachments->lastError());

    STAGE17A_ATTACH_CHECK(attachments->deleteAttachments(both) && viewCount(0) && document->dirty() && undo->canUndo(),
                         "Multi-delete did not empty the open list or record an undo step");
    undo->undo();
    STAGE17A_ATTACH_CHECK(dialog->property("opened").toBool() && viewCount(2) && !document->dirty(),
                         "VM undo did not repopulate the still-open dialog and return to the saved state");
    auto *deleteButton = control(QStringLiteral("attachmentsDeleteButton"));
    STAGE17A_ATTACH_CHECK(deleteButton && !deleteButton->isEnabled() && selectRow(fontRow()) && click(deleteButton) &&
                         viewCount(1) && fontRow() < 0 && graphicRow() >= 0 && !deleteButton->isEnabled() &&
                         document->dirty() && closeDialog(), "QML deletion did not remove only the selected row and reset selection");
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    STAGE17A_ATTACH_CHECK(QTest::qWaitFor([&] { return attachments->rowCount() == 2 && !document->dirty(); }, timeout) &&
                         undo->canRedo() && openDialog() && viewCount(2) && fontRow() >= 0 && graphicRow() >= 0,
                         "Ctrl+Z did not restore the deleted attachment through the application shortcut");

    const auto unsafePath = dir.filePath(QStringLiteral("unsafe.ass"));
    const auto blockedName = QStringLiteral("stage17a-outside.bin");
    const auto outsidePath = dir.filePath(blockedName);
    const auto safeName = QStringLiteral("stage17a-safe.bin");
    const auto blockedSinglePath = dir.filePath(QStringLiteral("blocked-single.bin"));
    // "!!!!" is ASS attachment encoding for three zero bytes. Parsing, not fixture setup, creates the actual rows.
    const QByteArray unsafeAss = "[Script Info]\nScriptType: v4.00+\n\n[Graphics]\n"
                                 "filename: stage17a-safe.bin\n!!!!\n"
                                 "filename: ../stage17a-outside.bin\n!!!!\n"
                                 "filename: ..\\stage17a-backslash.bin\n!!!!\n";
    STAGE17A_ATTACH_CHECK(closeDialog() && writeBytes(unsafePath, unsafeAss) && document->openLocalPath(unsafePath) &&
                         openDialog() && viewCount(3), "Malicious ASS fixture was not parsed into three real attachments");
    const int safeRow = rowNamed(attachments, nameRole, safeName);
    STAGE17A_ATTACH_CHECK(safeRow >= 0 && attachments->validateExtraction({safeRow}), "The benign fixture entry was rejected");
    // Verify the QML picker routing without invoking a native chooser: unsafe rows must stop before open().
    auto *savePicker = dialog->findChild<QObject *>(QStringLiteral("attachmentsSavePicker"));
    auto *folderPicker = dialog->findChild<QObject *>(QStringLiteral("attachmentsFolderPicker"));
    auto *extractButton = control(QStringLiteral("attachmentsExtractButton"));
    auto *errorLabel = control(QStringLiteral("attachmentsError"));
    const auto pickersClosed = [&] {
        return savePicker && folderPicker && !savePicker->property("visible").toBool() && !folderPicker->property("visible").toBool();
    };
    const auto visibleError = [&] {
        return errorLabel && errorLabel->isVisible() && !attachments->lastError().isEmpty() &&
               errorLabel->property("text").toString() == attachments->lastError();
    };
    STAGE17A_ATTACH_CHECK(pickersClosed() && extractButton && !extractButton->isEnabled() &&
                         selectRow(rowNamed(attachments, nameRole, QStringLiteral("../stage17a-outside.bin"))) &&
                         click(extractButton) && visibleError() && pickersClosed() && clearError() &&
                         selectRow(safeRow) && click(extractButton) && visibleError() && pickersClosed() && clearError(),
                         "QML extraction did not block unsafe single/batch picker routes with a visible error");
    for (const auto &name : {QStringLiteral("../stage17a-outside.bin"), QStringLiteral("..\\stage17a-backslash.bin")}) {
        const int badRow = rowNamed(attachments, nameRole, name);
        STAGE17A_ATTACH_CHECK(badRow >= 0 && !attachments->validateExtraction({badRow}) && !attachments->lastError().isEmpty() && clearError(),
                             "Unsafe filename passed validation, was not shown verbatim, or did not report an error:" << name);
        STAGE17A_ATTACH_CHECK(attachments->extractionFileUrl(badRow).isEmpty() && !attachments->lastError().isEmpty() && clearError(),
                             "An unsafe attachment was offered to the single-file picker:" << name);
        STAGE17A_ATTACH_CHECK(!attachments->extractAttachment(badRow, QUrl::fromLocalFile(blockedSinglePath)) &&
                             !attachments->lastError().isEmpty() && !QFileInfo::exists(blockedSinglePath) && clearError(),
                             "Single-file extraction did not reject an unsafe source name:" << name);
        STAGE17A_ATTACH_CHECK(!attachments->extractAttachments(QVariantList{safeRow, badRow}, batchUrl) && !attachments->lastError().isEmpty() &&
                             !QFileInfo::exists(outsidePath) && !QFileInfo::exists(dir.filePath(QStringLiteral("stage17a-backslash.bin"))) &&
                             !QFileInfo::exists(batch.filePath(safeName)) && clearError(),
                             "Mixed batch was not rejected before writing files:" << name);
    }
    STAGE17A_ATTACH_CHECK(!document->dirty() && closeDialog() && document->openFile(QUrl::fromLocalFile(assPath)) &&
                         attachments->rowCount() == 2 && !document->dirty(), "Could not restore the clean valid document");

    fixture.setAutoRemove(false); // The caller owns scratchPath; keep the loaded ASS on disk.
#undef STAGE17A_ATTACH_CHECK
    return 0;
}
