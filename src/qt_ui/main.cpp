// Must come before any Qt header: agi::signal (via ass_file.h) has a member
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../ass_file.h"

#include "aegisub_core.h"
#include "viewmodels/about_vm.h"
#include "viewmodels/app_info_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/search_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/undo_manager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace {

/// Routes Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z to the UndoManager.
///
/// A QML Action.shortcut is not enough here: QQuickTextInput implements its own
/// undo/redo and accepts the ShortcutOverride event for those keys, which
/// silently swallows the menu shortcut whenever an edit field has focus. An
/// application-wide filter sees the ShortcutOverride first, accepts it (so no
/// shortcut fires at all) and then handles the KeyPress itself, which gives
/// exactly one undo per key press regardless of the focus.
class UndoShortcutFilter final : public QObject {
public:
    explicit UndoShortcutFilter(UndoManager *undo, QObject *parent = nullptr)
    : QObject(parent)
    , undo_(undo)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        Q_UNUSED(watched);

        const QEvent::Type type = event->type();
        if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
            return false;

        auto *key_event = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers mods = key_event->modifiers();
        if (mods != Qt::ControlModifier && mods != (Qt::ControlModifier | Qt::ShiftModifier))
            return false;

        const bool shift = mods.testFlag(Qt::ShiftModifier);
        const int key = key_event->key();
        const bool is_undo = key == Qt::Key_Z && !shift;
        const bool is_redo = (key == Qt::Key_Y && !shift) || (key == Qt::Key_Z && shift);
        if (!is_undo && !is_redo)
            return false;

        if (type == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }

        // Auto-repeat would blow through the whole undo stack in a flash.
        if (!key_event->isAutoRepeat()) {
            if (is_undo)
                undo_->undo();
            else
                undo_->redo();
        }
        event->accept();
        return true;
    }

private:
    UndoManager *undo_;
};

/// Set as the `objectName` of the body TextArea in EditBox.qml.
constexpr const char kBodyEditorObjectName[] = "editBoxTextArea";

/// Forwards Enter / Shift+Enter from the body TextArea to EditBoxViewModel.
///
/// QQuickTextArea consumes Return itself (it inserts a newline and accepts the
/// event) before QML `Keys` attached handlers are consulted, so
/// `Keys.onReturnPressed` on the TextArea never fires. An application-level
/// filter sees the KeyPress first and handles it deterministically.
class BodyEnterFilter final : public QObject {
public:
    explicit BodyEnterFilter(EditBoxViewModel *editBox, QObject *parent = nullptr)
    : QObject(parent)
    , editBox_(editBox)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        Q_UNUSED(watched);

        if (event->type() != QEvent::KeyPress)
            return false;

        auto *key_event = static_cast<QKeyEvent *>(event);
        if (key_event->key() != Qt::Key_Return && key_event->key() != Qt::Key_Enter)
            return false;

        QObject *focus = QGuiApplication::focusObject();
        if (!focus || focus->objectName() != QLatin1String(kBodyEditorObjectName))
            return false;

        if (!key_event->isAutoRepeat())
            editBox_->handleEnterKey(key_event->modifiers().testFlag(Qt::ShiftModifier));

        event->accept();
        return true;
    }

private:
    EditBoxViewModel *editBox_;
};

} // namespace

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("aegisub-qt");

    // Bring up the libaegisub core (paths + option store) before creating any
    // view model that reads configuration.
    QString core_error;
    const bool core_ok = aegisub_qt::InitializeCore(&core_error);
    if (!core_ok)
        qWarning() << "Aegisub core initialization failed:" << core_error;

    // The document currently being edited. DocumentViewModel fills it with
    // either the default new-file content or a file passed on the command line.
    AssFile script;

    // The undo stack must exist before the view models: it owns the clean
    // baseline that DocumentViewModel::dirty is derived from.
    UndoManager undoManager(&script);

    TranslationService i18n;
    AppInfoViewModel appInfoVM;
    AboutViewModel aboutVM(&i18n);
    PreferencesViewModel preferencesVM;
    PropertiesViewModel propertiesVM(&script, &i18n, &undoManager);
    // The grid model is constructed before the edit box so that it rebuilds its
    // row index first when UndoManager::restored fires (see undo_manager.h).
    SubtitleGridModel gridModel(&script, &undoManager);
    EditBoxViewModel editBoxVM(&gridModel, &script, &undoManager);
    DocumentViewModel documentVM(&script, &gridModel, &undoManager);
    SearchViewModel searchVM(&script, &gridModel);

    UndoShortcutFilter undoFilter(&undoManager);
    app.installEventFilter(&undoFilter);

    // The body TextArea eats Return itself; see BodyEnterFilter.
    BodyEnterFilter bodyEnterFilter(&editBoxVM);
    app.installEventFilter(&bodyEnterFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("i18n", &i18n);
    engine.rootContext()->setContextProperty("appInfo", &appInfoVM);
    engine.rootContext()->setContextProperty("aboutVM", &aboutVM);
    engine.rootContext()->setContextProperty("preferencesVM", &preferencesVM);
    engine.rootContext()->setContextProperty("propertiesVM", &propertiesVM);
    engine.rootContext()->setContextProperty("gridModel", &gridModel);
    engine.rootContext()->setContextProperty("editBoxVM", &editBoxVM);
    engine.rootContext()->setContextProperty("documentVM", &documentVM);
    engine.rootContext()->setContextProperty("undoVM", &undoManager);
    engine.rootContext()->setContextProperty("searchVM", &searchVM);

    // Optional first command line argument: a subtitle file to open.
    const QStringList args = QCoreApplication::arguments();
    const QString startup_file = args.size() > 1 ? args.at(1) : QString();
    if (!startup_file.isEmpty() && QFileInfo(startup_file).isFile()) {
        if (!documentVM.openLocalPath(startup_file))
            qWarning() << "Failed to open" << startup_file << ":" << documentVM.lastError();
    } else {
        if (!documentVM.newDocument())
            qWarning() << "Failed to create a new document:" << documentVM.lastError();
    }

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        QString error;
        if (!aegisub_qt::FlushOptions(&error))
            qWarning() << "Failed to flush options on exit:" << error;
    });

    return app.exec();
}
