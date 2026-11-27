// Must come before any Qt header: agi::signal (via ass_file.h) has a member
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../ass_file.h"

#include <libaegisub/exception.h>

#include "aegisub_core.h"
#include "automation/automation_vm.h"
#include "audio_waveform_item.h"
#include "video_bridge.h"
#include "video_viewport_item.h"
#include "visual_overlay_item.h"
#include "viewmodels/visual_tools_vm.h"
#include "viewmodels/about_vm.h"
#include "viewmodels/app_info_vm.h"
#include "viewmodels/audio_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/hotkey_vm.h"
#include "viewmodels/mru_vm.h"
#include "viewmodels/log_vm.h"
#include "viewmodels/theme_vm.h"
#include "ass_syntax_highlighter.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/resample_vm.h"
#include "viewmodels/search_vm.h"
#include "viewmodels/select_lines_vm.h"
#include "viewmodels/paste_over_vm.h"
#include "viewmodels/attachments_vm.h"
#include "viewmodels/fonts_collector_vm.h"
#include "viewmodels/shift_times_vm.h"
#include "viewmodels/styles_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/undo_manager.h"
#include "viewmodels/video_vm.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>
#include <QWindow>

#include <exception>

namespace {

QString medusaKey() { return QStringLiteral("Audio/Medusa Timing Hotkeys"); }

void showCommandUi(QQuickWindow *window, const char *id) {
    if (!QMetaObject::invokeMethod(window, "showCommandUi", Q_ARG(QVariant, QVariant(QString::fromLatin1(id)))))
        qWarning() << "Could not invoke visual action for" << id;
}

void focusVideoSeek(QQuickWindow *window) {
    if (auto *seek = window->findChild<QQuickItem *>(QStringLiteral("videoSeekSlider")))
        seek->forceActiveFocus(Qt::ShortcutFocusReason);
}

bool hasBodyFocus() {
    const auto *focus = QGuiApplication::focusObject();
    return focus && focus->objectName() == QStringLiteral("editBoxTextArea");
}

void registerHotkeyCommands(HotkeyViewModel &hotkeyVM, QQuickWindow *main,
                            PreferencesViewModel &preferencesVM, ThemeViewModel &themeVM,
                            SubtitleGridModel &gridModel, EditBoxViewModel &editBoxVM,
                            UndoManager &undoManager, VideoViewModel &videoVM,
                            AudioViewModel &audioVM, VisualToolsViewModel &visualToolsVM,
                            aegisub_qt::automation::AutomationViewModel &automationVM) {
    auto command = [&](const char *id, const char *context, std::function<void()> action,
                       std::function<bool()> enabled = {}) {
        hotkeyVM.registerCommand(QString::fromLatin1(id), QString::fromLatin1(context),
                                 std::move(action), std::move(enabled));
    };
#include "hotkey_commands.inc"
    hotkeyVM.setSoftLineBreakHandler([&editBoxVM] { editBoxVM.handleEnterKey(true); });
}

} // namespace

#ifdef AEGISUB_STAGE15_TEST
#include <QTemporaryDir>
#include <cstdio>
int runStage15IntegrationTest(QQmlApplicationEngine &engine);
int runStage15BIntegrationTest(QQmlApplicationEngine &engine);
int runStage15BReloadTest(QQmlApplicationEngine &engine);
int runStage16AIntegrationTest(QQmlApplicationEngine &engine);
int runStage16BIntegrationTest(QQmlApplicationEngine &engine);
int runStage17AIntegrationTest(QQmlApplicationEngine &engine, bool restart);
int RunStage17BIntegrationTest(QQmlApplicationEngine &, DocumentViewModel &, FontsCollectorViewModel &);
int RunStage17BRestartCheck(FontsCollectorViewModel &);
int runStage17CIntegrationTest(QQmlApplicationEngine &engine);
int runStage18AIntegrationTest(QQmlApplicationEngine &engine, bool restart);
bool writeStage18BHotkeyFixture();
int runStage18BIntegrationTest(QGuiApplication &, QQmlApplicationEngine &, bool restart);
#endif

int main(int argc, char *argv[]) {
#ifdef AEGISUB_STAGE15_TEST
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
        std::fprintf(type == QtInfoMsg || type == QtDebugMsg ? stdout : stderr,
                     "%s\n", qPrintable(message));
    });
    qunsetenv("AEGISUB_QT_LANGUAGE");
    bool reloadTest = false;
    bool automationTest = false;
    bool automationInteractiveTest = false;
    bool stage17ATest = false;
    bool stage17ARestart = false;
    bool stage17BTest = false;
    bool stage17BRestart = false;
    bool stage17CTest = false;
    bool stage18ATest = false;
    bool stage18ARestart = false;
    bool stage18BTest = false;
    bool stage18BRestart = false;
    for (int i = 1; i < argc; ++i) {
        reloadTest |= QByteArray(argv[i]) == "--stage15b-reload";
        automationTest |= QByteArray(argv[i]) == "--stage16a";
        automationInteractiveTest |= QByteArray(argv[i]) == "--stage16b";
        stage17ATest |= QByteArray(argv[i]) == "--stage17a";
        stage17ARestart |= QByteArray(argv[i]) == "--stage17a-restart";
        stage17BTest |= QByteArray(argv[i]) == "--stage17b";
        stage17BRestart |= QByteArray(argv[i]) == "--stage17b-restart";
        stage17CTest |= QByteArray(argv[i]) == "--stage17c";
        stage18ATest |= QByteArray(argv[i]) == "--stage18a";
        stage18ARestart |= QByteArray(argv[i]) == "--stage18a-restart";
        stage18BTest |= QByteArray(argv[i]) == "--stage18b";
        stage18BRestart |= QByteArray(argv[i]) == "--stage18b-restart";
    }
    reloadTest |= stage17ARestart || stage17BRestart || stage18ARestart || stage18BRestart;
    QTemporaryDir testConfig(QStringLiteral("stage15-config-XXXXXX"));
    if (!reloadTest) {
        if (!testConfig.isValid()) {
            qCritical() << testConfig.errorString();
            return 1;
        }
        qputenv("AEGISUB_QT_CONFIG_DIR", QFileInfo(testConfig.path()).absoluteFilePath().toUtf8());
    } else if (qEnvironmentVariableIsEmpty("AEGISUB_QT_CONFIG_DIR")) {
        return 1;
    }
#endif
    QGuiApplication app(argc, argv);
    app.setApplicationName("aegisub-qt");

    // Bring up the libaegisub core (paths + option store) before creating any
    // view model that reads configuration.
    QString core_error;
    const bool core_ok = aegisub_qt::InitializeCore(&core_error);
    if (!core_ok)
        qWarning() << "Aegisub core initialization failed:" << core_error;

#ifdef AEGISUB_STAGE15_TEST
    if (stage18BTest && !writeStage18BHotkeyFixture()) return 1;
#endif

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
#ifdef AEGISUB_STAGE15_TEST
    if (!reloadTest && !i18n.setLanguage(QStringLiteral("en_US"))) {
        qCritical() << "Cannot set isolated test language:" << i18n.error();
        return 1;
    }
#endif
    AppInfoViewModel appInfoVM;
    AboutViewModel aboutVM(&i18n);
    PreferencesViewModel preferencesVM;
    HotkeyViewModel hotkeyVM;
    ThemeViewModel themeVM;
    PropertiesViewModel propertiesVM(&script, &i18n, &undoManager);
    // The grid model is constructed before the edit box so that it rebuilds its
    // row index first when UndoManager::restored fires (see undo_manager.h).
    SubtitleGridModel gridModel(&script, &undoManager);
    EditBoxViewModel editBoxVM(&gridModel, &script, &undoManager);
    DocumentViewModel documentVM(&script, &gridModel, &undoManager);
    SearchViewModel searchVM(&script, &gridModel);
    SelectLinesViewModel selectLinesVM(&script, &gridModel);
    PasteOverViewModel pasteOverVM(&script, &gridModel);
    AttachmentsViewModel attachmentsVM(&script, &undoManager, &documentVM);
    FontsCollectorViewModel fontsCollectorVM(&script, &documentVM);
    StylesViewModel stylesVM(&script, &undoManager);
    VideoViewModel videoVM(&script, &gridModel, &undoManager);
    AudioViewModel audioVM(&script, &gridModel, &undoManager);
    MruViewModel mruVM;
    LogViewModel logVM;
    aegisub_qt::automation::AutomationViewModel automationVM(script, gridModel, documentVM, videoVM, audioVM, i18n);
    QObject::connect(&documentVM, &DocumentViewModel::fileOpened, &mruVM, &MruViewModel::recordSubtitle);
    QObject::connect(&videoVM, &VideoViewModel::fileOpened, &mruVM, &MruViewModel::recordVideo);
    QObject::connect(&audioVM, &AudioViewModel::fileOpened, &mruVM, &MruViewModel::recordAudio);
    QObject::connect(&videoVM, &VideoViewModel::videoNameChanged, &documentVM, [&] {
        documentVM.setLinkedVideoPath(videoVM.filePath());
    });
    QObject::connect(&audioVM, &AudioViewModel::audioChanged, &documentVM, [&] {
        documentVM.setLinkedAudioPath(audioVM.filePath());
    });
    auto recordVideoUiState = [&] {
        documentVM.setVideoUiState(videoVM.hasVideo() ? videoVM.zoomPercent() / 100. : 0.,
                                   videoVM.hasVideo() ? videoVM.currentFrame() : 0);
    };
    QObject::connect(&videoVM, &VideoViewModel::currentFrameChanged, &documentVM, recordVideoUiState);
    QObject::connect(&videoVM, &VideoViewModel::zoomPercentChanged, &documentVM, recordVideoUiState);
    auto syncGridPreferences = [&] {
        gridModel.setHideOverridesChar(preferencesVM.hideOverridesChar());
        gridModel.setCpsIgnoreOptions(preferencesVM.ignoreWhitespace(), preferencesVM.ignorePunctuation());
    };
    syncGridPreferences();
    QObject::connect(&preferencesVM, &PreferencesViewModel::optionsChanged, &gridModel, syncGridPreferences);
    // A/V synchronised playback: the video transport starts the audio player at
    // the current video position and then follows the audio clock.
    videoVM.setAudioViewModel(&audioVM);
    gridModel.setVideoViewModel(&videoVM);
    ShiftTimesViewModel shiftTimesVM(&script, &gridModel, &videoVM);
    ResampleViewModel resampleVM(&script, &videoVM);
    VisualToolsViewModel visualToolsVM(&script, &gridModel, &videoVM, &undoManager);

    // New/Open does not go through AssFile::Commit, so the style index has to
    // be told about a fresh document explicitly.
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &stylesVM, &StylesViewModel::handleDocumentReload);
    // The video preview caches a libass track, so it needs the same treatment.
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &videoVM, &VideoViewModel::handleDocumentReload);
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &visualToolsVM, &VisualToolsViewModel::handleDocumentReload);
    // The audio panel caches the active line to draw its red/blue lines.
    QObject::connect(&documentVM, &DocumentViewModel::documentReloaded,
                     &audioVM, &AudioViewModel::handleDocumentReload);

    // Stage 11B: video keyframes drive the waveform overlay and the snapping.
    QObject::connect(&videoVM, &VideoViewModel::keyframeTimesChanged, &audioVM, [&] {
        audioVM.setKeyframeTimesMs(videoVM.keyframeTimesMsVector());
    });

    // QML-only wrappers that paint the media view models.
    qmlRegisterType<VideoViewportItem>("Aegisub", 1, 0, "VideoViewport");
    qmlRegisterType<VisualOverlayItem>("Aegisub", 1, 0, "VisualOverlay");
    qmlRegisterType<AudioWaveformItem>("Aegisub", 1, 0, "AudioWaveform");
    qmlRegisterType<AssSyntaxHighlighter>("Aegisub", 1, 0, "AssSyntaxHighlighter");

    QQmlApplicationEngine engine;
    i18n.setQmlEngine(&engine);
    QObject::connect(&i18n, &TranslationService::languageChanged, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("nativeWindowRaise", QGuiApplication::platformName() != QStringLiteral("offscreen"));
    engine.rootContext()->setContextProperty("i18n", &i18n);
    engine.rootContext()->setContextProperty("appInfo", &appInfoVM);
    engine.rootContext()->setContextProperty("aboutVM", &aboutVM);
    engine.rootContext()->setContextProperty("preferencesVM", &preferencesVM);
    engine.rootContext()->setContextProperty("hotkeyVM", &hotkeyVM);
    engine.rootContext()->setContextProperty("mruVM", &mruVM);
    engine.rootContext()->setContextProperty("logVM", &logVM);
    engine.rootContext()->setContextProperty("automationVM", &automationVM);
    engine.rootContext()->setContextProperty("themeVM", &themeVM);
    engine.rootContext()->setContextProperty("propertiesVM", &propertiesVM);
    engine.rootContext()->setContextProperty("gridModel", &gridModel);
    engine.rootContext()->setContextProperty("editBoxVM", &editBoxVM);
    engine.rootContext()->setContextProperty("documentVM", &documentVM);
    engine.rootContext()->setContextProperty("undoVM", &undoManager);
    engine.rootContext()->setContextProperty("searchVM", &searchVM);
    engine.rootContext()->setContextProperty("selectLinesVM", &selectLinesVM);
    engine.rootContext()->setContextProperty("pasteOverVM", &pasteOverVM);
    engine.rootContext()->setContextProperty("attachmentsVM", &attachmentsVM);
    engine.rootContext()->setContextProperty("fontsCollectorVM", &fontsCollectorVM);
    engine.rootContext()->setContextProperty("stylesVM", &stylesVM);
    engine.rootContext()->setContextProperty("videoVM", &videoVM);
    engine.rootContext()->setContextProperty("visualToolsVM", &visualToolsVM);
    engine.rootContext()->setContextProperty("audioVM", &audioVM);
    engine.rootContext()->setContextProperty("shiftTimesVM", &shiftTimesVM);
    engine.rootContext()->setContextProperty("resampleVM", &resampleVM);

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

    const bool hasStartupSubtitle = !startup_file.isEmpty() && QFileInfo(startup_file).isFile();
    if (!hasStartupSubtitle && !documentVM.newDocument())
        qWarning() << "Failed to create a new document:" << documentVM.lastError();

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (hasStartupSubtitle) {
        auto *root = engine.rootObjects().first();
        root->setProperty("skipLinkedVideo", QFileInfo(startup_video).isFile());
        root->setProperty("skipLinkedAudio", QFileInfo(startup_audio).isFile());
        if (!documentVM.openLocalPath(startup_file))
            qWarning() << "Failed to open" << startup_file << ":" << documentVM.lastError();
        root->setProperty("skipLinkedVideo", false);
        root->setProperty("skipLinkedAudio", false);
    }

    auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!mainWindow)
        return 1;
    registerHotkeyCommands(hotkeyVM, mainWindow, preferencesVM, themeVM, gridModel,
                           editBoxVM, undoManager, videoVM, audioVM, visualToolsVM, automationVM);
    hotkeyVM.attachMainWindow(mainWindow);

    // Open the command-line media only after QML is up: VideoPanel would miss
    // the frameReady() signal emitted during a pre-engine open and show a black
    // viewport until the next frame change. Exceptions must not escape these
    // calls: they run before the event loop and an uncaught C++ exception would
    // call std::terminate -> abort (see docs/qt6-frontend.md pitfall 56).
    if (!startup_video.isEmpty() && QFileInfo(startup_video).isFile()) {
        try {
            if (!videoVM.openLocalPath(startup_video))
                qWarning() << "Failed to open video" << startup_video << ":" << videoVM.lastError();
        } catch (agi::Exception const &ex) {
            // agi::Exception does not derive from std::exception.
            qWarning() << "Failed to open video" << startup_video << ":"
                       << QString::fromStdString(ex.GetMessage());
        } catch (std::exception const &ex) {
            qWarning() << "Failed to open video" << startup_video << ":" << ex.what();
        } catch (...) {
            qWarning() << "Failed to open video" << startup_video << ": unknown error";
        }
    }

    if (!startup_audio.isEmpty() && QFileInfo(startup_audio).isFile()) {
        try {
            if (!audioVM.openLocalPath(startup_audio))
                qWarning() << "Failed to open audio" << startup_audio << ":" << audioVM.lastError();
        } catch (agi::Exception const &ex) {
            qWarning() << "Failed to open audio" << startup_audio << ":"
                       << QString::fromStdString(ex.GetMessage());
        } catch (std::exception const &ex) {
            qWarning() << "Failed to open audio" << startup_audio << ":" << ex.what();
        } catch (...) {
            qWarning() << "Failed to open audio" << startup_audio << ": unknown error";
        }
    }

    automationVM.reloadAutoload();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        QString error;
        if (!aegisub_qt::FlushOptions(&error))
            qWarning() << "Failed to flush options on exit:" << error;
    });

#ifdef AEGISUB_STAGE15_TEST
    if (stage18BTest || stage18BRestart) return runStage18BIntegrationTest(app, engine, stage18BRestart);
    if (stage18ATest || stage18ARestart) return runStage18AIntegrationTest(engine, stage18ARestart);
    if (stage17CTest) return runStage17CIntegrationTest(engine);
    if (stage17BRestart) return RunStage17BRestartCheck(fontsCollectorVM);
    if (stage17BTest) return RunStage17BIntegrationTest(engine, documentVM, fontsCollectorVM);
    if (stage17ATest || stage17ARestart) return runStage17AIntegrationTest(engine, stage17ARestart);
    if (automationInteractiveTest) return runStage16BIntegrationTest(engine);
    if (automationTest) return runStage16AIntegrationTest(engine);
    if (reloadTest) return runStage15BReloadTest(engine);
    if (const int result = runStage15IntegrationTest(engine)) return result;
    return runStage15BIntegrationTest(engine);
#else
    return app.exec();
#endif
}
