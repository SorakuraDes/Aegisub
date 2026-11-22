// Must come before any Qt header: agi::signal (via ass_file.h) has a member
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../ass_file.h"

#include "aegisub_core.h"
#include "audio_waveform_item.h"
#include "video_bridge.h"
#include "video_viewport_item.h"
#include "viewmodels/about_vm.h"
#include "viewmodels/app_info_vm.h"
#include "viewmodels/audio_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/search_vm.h"
#include "viewmodels/shift_times_vm.h"
#include "viewmodels/styles_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/undo_manager.h"
#include "viewmodels/video_vm.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>

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

/// Routes Ctrl+S / Ctrl+Shift+S to the QML window.
///
/// Registering both as QML `Shortcut`s does not work: Qt's shortcut matcher
/// treats Ctrl+S and Ctrl+Shift+S as an ambiguous pair for the same key (the
/// Shift modifier is not distinguishing for letter keys), so neither fires.
/// An application-level filter accepts the ShortcutOverride (suppressing the
/// QML shortcuts entirely) and then dispatches the two cases itself.
class FileShortcutFilter final : public QObject {
public:
    explicit FileShortcutFilter(QObject *rootObject, QObject *parent = nullptr)
    : QObject(parent)
    , root_(rootObject)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        Q_UNUSED(watched);

        const QEvent::Type type = event->type();
        if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
            return false;

        auto *key_event = static_cast<QKeyEvent *>(event);
        if (key_event->key() != Qt::Key_S)
            return false;

        const Qt::KeyboardModifiers mods = key_event->modifiers();
        if (mods != Qt::ControlModifier && mods != (Qt::ControlModifier | Qt::ShiftModifier))
            return false;

        if (type == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }

        if (!key_event->isAutoRepeat() && root_) {
            const char *method = mods.testFlag(Qt::ShiftModifier) ? "openSaveAsDialog"
                                                                  : "saveDocument";
            if (!QMetaObject::invokeMethod(root_, method, Qt::DirectConnection))
                qWarning() << "aegisub-qt: Main.qml has no" << method << "function";
        }
        event->accept();
        return true;
    }

private:
    QObject *root_;
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

    // libass library + the background font-cache build. Doing this at startup
    // (like src/main.cpp does for the wx frontend) keeps the first video frame
    // from paying for the font index.
    if (core_ok)
        aegisub_qt::VideoBridge::Initialize();

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
    StylesViewModel stylesVM(&script, &undoManager);
    VideoViewModel videoVM(&script, &gridModel, &undoManager);
    AudioViewModel audioVM(&script, &gridModel, &undoManager);
    // A/V synchronised playback: the video transport starts the audio player at
    // the current video position and then follows the audio clock.
    videoVM.setAudioViewModel(&audioVM);
    ShiftTimesViewModel shiftTimesVM(&script, &gridModel, &videoVM);

    // New/Open does not go through AssFile::Commit, so the style index has to
    // be told about a fresh document explicitly.
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &stylesVM, &StylesViewModel::handleDocumentReload);
    // The video preview caches a libass track, so it needs the same treatment.
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &videoVM, &VideoViewModel::handleDocumentReload);
    // The audio panel caches the active line to draw its red/blue lines.
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &audioVM, &AudioViewModel::handleDocumentReload);

    UndoShortcutFilter undoFilter(&undoManager);
    app.installEventFilter(&undoFilter);

    // The body TextArea eats Return itself; see BodyEnterFilter.
    BodyEnterFilter bodyEnterFilter(&editBoxVM);
    app.installEventFilter(&bodyEnterFilter);

    // QML-only wrappers that paint the media view models.
    qmlRegisterType<VideoViewportItem>("Aegisub", 1, 0, "VideoViewport");
    qmlRegisterType<AudioWaveformItem>("Aegisub", 1, 0, "AudioWaveform");

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
    engine.rootContext()->setContextProperty("stylesVM", &stylesVM);
    engine.rootContext()->setContextProperty("videoVM", &videoVM);
    engine.rootContext()->setContextProperty("audioVM", &audioVM);
    engine.rootContext()->setContextProperty("shiftTimesVM", &shiftTimesVM);

    // Command line: an optional subtitle file plus "--video <path>" and
    // "--audio <path>", which lets the automated checks open everything at once.
    const QStringList args = QCoreApplication::arguments();
    QString startup_file;
    QString startup_video;
    QString startup_audio;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--video") && i + 1 < args.size()) {
            startup_video = args.at(++i);
            continue;
        }
        if (arg == QLatin1String("--audio") && i + 1 < args.size()) {
            startup_audio = args.at(++i);
            continue;
        }
        if (arg.startsWith(QLatin1Char('-')))
            continue;
        if (startup_file.isEmpty())
            startup_file = arg;
    }

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

    // Ctrl+S / Ctrl+Shift+S must be dispatched here; see FileShortcutFilter.
    if (QObject *root_object = engine.rootObjects().first()) {
        auto *file_filter = new FileShortcutFilter(root_object, &app);
        app.installEventFilter(file_filter);
    }

    // Open the command-line media only after QML is up: VideoPanel would miss
    // the frameReady() signal emitted during a pre-engine open and show a black
    // viewport until the next frame change.
    if (!startup_video.isEmpty() && QFileInfo(startup_video).isFile()) {
        if (!videoVM.openLocalPath(startup_video))
            qWarning() << "Failed to open video" << startup_video << ":" << videoVM.lastError();
    }

    if (!startup_audio.isEmpty() && QFileInfo(startup_audio).isFile()) {
        if (!audioVM.openLocalPath(startup_audio))
            qWarning() << "Failed to open audio" << startup_audio << ":" << audioVM.lastError();
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        QString error;
        if (!aegisub_qt::FlushOptions(&error))
            qWarning() << "Failed to flush options on exit:" << error;
    });

    return app.exec();
}
