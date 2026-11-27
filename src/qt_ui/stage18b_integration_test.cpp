#include "viewmodels/subtitle_grid_model.h" // Pull in agi::signal before Qt's slots macro.
#include "aegisub_core.h"
#include "stage17a_attachments_test.h"
#include "viewmodels/theme_vm.h"
#include "viewmodels/video_vm.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>

namespace {
using namespace stage17a_attachments_test;
#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 18B:" << __LINE__ << message; return 1; \
} } while (false)

constexpr auto findId = "subtitle/find";
constexpr auto unknownId = "other_frontend/stage18b";
constexpr auto unknownContext = "Other Frontend";
constexpr auto bindings = R"JSON({
    "Always": { "video/frame/next": ["Ctrl-8"] },
    "Default": {
        "app/options": ["Alt-O"],
        "subtitle/find": ["F6"],
        "edit/find_replace": ["Ctrl-H"],
        "edit/line/copy": ["Ctrl-C"],
        "edit/line/cut": ["Ctrl-X"],
        "edit/line/paste": ["Ctrl-V"],
        "edit/line/paste/over": ["Ctrl-Shift-V"],
        "edit/undo": ["Ctrl-Z"],
        "video/jump": ["Ctrl-G"],
        "video/frame/prev": ["Ctrl-6", "Ctrl-S"],
        "video/frame/next": ["Ctrl-Shift-S"],
        "tool/kanji": ["Ctrl-J"],
        "other_frontend/stage18b": ["Ctrl-Shift-F11", "Ctrl-KP_Multiply"]
    },
    "Subtitle Grid": {
        "video/frame/prev": ["Ctrl-8", "Enter"],
        "video/frame/next": ["Ctrl-KP_6", "KP_Enter"]
    },
    "Other Frontend": { "other_frontend/stage18b": ["F10", "Ctrl-Alt-F12"] }
})JSON";

constexpr auto fixture = R"ASS([Script Info]
ScriptType: v4.00+
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.10,0:00:01.10,Default,,0,0,0,,first line
Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,second line
)ASS";

QDir configDir() { return QFileInfo(aegisub_qt::OptionsFilePath()).absoluteDir(); }
QString hotkeyPath() { return configDir().filePath(QStringLiteral("hotkey.json")); }

QJsonObject readBindings() {
    QFile file(hotkeyPath());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}

QJsonArray keys(const QJsonObject &map, const char *context, const char *command) {
    return map.value(QLatin1String(context)).toObject().value(QLatin1String(command)).toArray();
}

bool unknownRetained(const QJsonObject &map) {
    const auto seed = QJsonDocument::fromJson(bindings).object();
    return keys(map, "Default", unknownId) == keys(seed, "Default", unknownId) &&
           keys(map, "Default", "tool/kanji") == keys(seed, "Default", "tool/kanji") &&
           map.value(QLatin1String(unknownContext)) == seed.value(QLatin1String(unknownContext));
}

QQuickWindow *relatedWindow(QQuickWindow *main, const char *name) {
    return qobject_cast<QQuickWindow *>(object(main->property(name)));
}

QQuickItem *control(QQuickWindow *window, const QString &name) {
    return window ? visualItem(window->contentItem(), name) : nullptr;
}

QObject *menuItem(QQuickWindow *window, const char *command) {
    auto *bar = object(window->property("menuBar"));
    if (!bar) return nullptr;
    for (int i = 0; i < bar->property("count").toInt(); ++i) {
        auto *menu = qmlObject(bar, QStringLiteral("menuAt(%1)").arg(i));
        if (!menu) continue;
        for (int j = 0; j < menu->property("count").toInt(); ++j) {
            auto *item = qmlObject(menu, QStringLiteral("itemAt(%1)").arg(j));
            if (item && item->property("commandId").toString() == QLatin1String(command))
                return item;
        }
    }
    return nullptr;
}

QString menuLabel(QQuickWindow *window, const char *command) {
    auto *item = menuItem(window, command);
    return item ? item->property("shortcutText").toString() : QString{};
}

bool focus(QQuickWindow *window, QQuickItem *item = nullptr) {
    window->requestActivate();
    if (item) item->forceActiveFocus();
    return QTest::qWaitFor([&] { return window->isActive() && (!item || item->hasActiveFocus()); }, timeout);
}

void key(QQuickWindow *window, Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(window, code, modifiers);
    QCoreApplication::processEvents();
}

bool isVisible(const QObject *obj) { return obj && obj->property("visible").toBool(); }
}

// Only Stage 18B seeds its isolated config. A user hotkey.json replaces defaults.
bool writeStage18BHotkeyFixture() {
    return !qEnvironmentVariableIsEmpty("AEGISUB_QT_CONFIG_DIR") && !aegisub_qt::OptionsFilePath().isEmpty() &&
           writeBytes(hotkeyPath(), bindings);
}

int runStage18BIntegrationTest(QGuiApplication &app, QQmlApplicationEngine &engine, bool restart) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    const auto vm = [&](const char *name) { return object(engine.rootContext()->contextProperty(QLatin1String(name))); };
    auto *grid = qobject_cast<SubtitleGridModel *>(vm("gridModel"));
    auto *document = qobject_cast<DocumentViewModel *>(vm("documentVM"));
    auto *theme = qobject_cast<ThemeViewModel *>(vm("themeVM"));
    auto *video = qobject_cast<VideoViewModel *>(vm("videoVM"));
    CHECK(window && grid && document && theme && video, "missing production window or VMs");
    auto *list = control(window, QStringLiteral("subtitleGridList"));
    auto *body = control(window, QStringLiteral("editBoxTextArea"));
    CHECK(list && body && focus(window, list), "main grid did not take focus");
    const auto searchWindow = [&] { return relatedWindow(window, "searchWindow"); };
    const auto searchVisible = [&] { return isVisible(searchWindow()); };
    const auto settingsWindow = [&] { return relatedWindow(window, "preferencesWindow"); };
    const auto closeWindows = qScopeGuard([&] {
        if (auto *search = searchWindow()) search->close();
        if (auto *settings = settingsWindow()) settings->close();
    });
    const auto expectFind = [&](Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        if (!focus(window, list) || searchVisible()) return false;
        key(window, code, modifiers);
        return QTest::qWaitFor(searchVisible, timeout);
    };
    const auto savedFind = QJsonArray{QStringLiteral("Ctrl-K")};
    CHECK(unknownRetained(readBindings()), "fixture's unknown commands/contexts are missing on disk");
    if (restart) {
        CHECK(keys(readBindings(), "Default", findId) == savedFind, "wrong on-disk assignment after restart");
        CHECK(menuLabel(window, findId).contains(QStringLiteral("Ctrl+K")), "new shortcut label did not reload");
        key(window, Qt::Key_F6);
        key(window, Qt::Key_F, Qt::ControlModifier);
        CHECK(!searchVisible(), "obsolete or omitted Find shortcut fired after restart");
        CHECK(expectFind(Qt::Key_K, Qt::ControlModifier), "new Find shortcut did not work in the restarted process");
        qInfo() << "Stage 18B restart passed";
        return 0;
    }

    const auto oldLabel = menuLabel(window, findId);
    CHECK(oldLabel == QStringLiteral("F6"), "startup fixture not reflected in Find menu" << oldLabel);
    key(window, Qt::Key_F, Qt::ControlModifier);
    CHECK(!searchVisible(), "omitted default Ctrl+F leaked into the replacement map");
    auto *findMenuItem = menuItem(window, findId);
    CHECK(findMenuItem && findMenuItem->property("hotkeyContext").toString() == QStringLiteral("Default") &&
          QMetaObject::invokeMethod(findMenuItem, "triggered") && QTest::qWaitFor(searchVisible, timeout),
          "Find menu did not dispatch its command");
    searchWindow()->close();
    CHECK(expectFind(Qt::Key_F6), "fixture's Find shortcut did not fire");
    searchWindow()->close();
    CHECK(focus(window, list), "main did not regain focus after Find");
    key(window, Qt::Key_O, Qt::AltModifier);
    CHECK(QTest::qWaitFor([&] { return isVisible(settingsWindow()); }, timeout), "Alt+O did not open Preferences");
    auto *settings = settingsWindow();
    settings->resize(940, 680);
    CHECK(focus(settings), "Preferences did not take focus");
    const auto pref = [&](const QString &name) { return control(settings, name); };
    const int hotkeysPage = settings->property("hotkeysPageIndex").toInt();
    auto *nav = pref(QStringLiteral("preferencesNavigation"));
    CHECK(hotkeysPage > 0 && nav && focus(settings, nav), "cannot focus Preferences navigation");
    // A ListView may instantiate delegates outside its clipped viewport.
    // Exercise the production keyboard navigation without clicking a clipped row.
    const int startPage = settings->property("page").toInt();
    for (int page = startPage; page < hotkeysPage; ++page) key(settings, Qt::Key_Down);
    CHECK(QTest::qWaitFor([&] { return settings->property("page").toInt() == hotkeysPage; }, timeout),
          "Preferences navigation did not select Hotkeys" << startPage << settings->property("page"));
    const auto field = QStringLiteral("hotkeySearch");
    CHECK(QTest::qWaitFor([&] { return isVisible(pref(field)); }, timeout),
          "Hotkeys page selected but search field was not created" << settings->property("hotkeysItem"));
    CHECK(focus(settings, pref(field)), "cannot focus Hotkeys search field");
    auto *clipboard = app.clipboard();
    auto *priorClipboard = new QMimeData;
    if (const auto *data = clipboard->mimeData()) {
        for (const auto &format : data->formats()) priorClipboard->setData(format, data->data(format));
    }
    const auto restoreClipboard = qScopeGuard([&] { clipboard->setMimeData(priorClipboard); });
    clipboard->setText(QString::fromLatin1(findId));
    key(settings, Qt::Key_V, Qt::ControlModifier);
    CHECK(pref(field)->property("text").toString() == QLatin1String(findId), "cannot filter Hotkeys by command ID");
    const auto findSuffix = QStringLiteral("/Default/subtitle/find");
    const auto edit = QStringLiteral("hotkeyEdit") + findSuffix + QStringLiteral("/0");
    const auto add = QStringLiteral("hotkeyAdd") + findSuffix;
    const auto recording = QStringLiteral("hotkeyRecordingPrompt");
    const auto startRecording = [&](const QString &name) {
        return QTest::qWaitFor([&] { return isVisible(pref(name)); }, timeout) && click(pref(name)) &&
               QTest::qWaitFor([&] { return isVisible(pref(recording)); }, timeout);
    };
    const auto notRecording = [&] { return !isVisible(pref(recording)); };
    const auto initialBindings = readBindings();
    CHECK(startRecording(edit), "cannot enter recording for the existing F6 binding");
    key(settings, Qt::Key_Control);
    CHECK(!notRecording() && readBindings() == initialBindings, "a bare modifier changed the binding");
    key(settings, Qt::Key_Escape);
    CHECK(isVisible(settings) && QTest::qWaitFor(notRecording, timeout) && readBindings() == initialBindings,
          "Escape closed Preferences or saved a canceled recording");
    CHECK(startRecording(edit), "cannot record conflicting replacement binding");
    key(settings, Qt::Key_H, Qt::ControlModifier);
    const auto error = QStringLiteral("hotkeyError");
    CHECK(QTest::qWaitFor([&] { return isVisible(pref(error)); }, timeout) && readBindings() == initialBindings,
          "a same-context conflict was not reported or it changed disk");
    if (!notRecording()) key(settings, Qt::Key_Escape);
    CHECK(QTest::qWaitFor(notRecording, timeout) && startRecording(edit), "cannot record replacement binding");
    key(settings, Qt::Key_K, Qt::ControlModifier);
    CHECK(QTest::qWaitFor(notRecording, timeout) && keys(readBindings(), "Default", findId) == savedFind,
          "Ctrl+K was not recorded and committed to disk");
    CHECK(QTest::qWaitFor([&] { return menuLabel(window, findId).contains(QStringLiteral("Ctrl+K")); }, timeout),
          "Find menu label did not update without reopening the main window");
    CHECK(startRecording(add), "cannot record an additional binding");
    key(settings, Qt::Key_F7);
    CHECK(QTest::qWaitFor(notRecording, timeout) &&
          keys(readBindings(), "Default", findId) == (QJsonArray{QStringLiteral("Ctrl-K"), QStringLiteral("F7")}),
          "F7 was not appended to the original Ctrl+K binding");
    const auto remove = QStringLiteral("hotkeyRemove") + findSuffix + QStringLiteral("/1");
    CHECK(QTest::qWaitFor([&] { return isVisible(pref(remove)); }, timeout) && click(pref(remove)) &&
          QTest::qWaitFor([&] { return keys(readBindings(), "Default", findId) == savedFind; }, timeout),
          "removing F7 changed or failed to preserve Ctrl+K");
    CHECK(unknownRetained(readBindings()), "editing a known binding lost unknown entries on disk");

    const auto shots = qEnvironmentVariable("AEGISUB_STAGE18B_SCREENSHOTS");
    if (!shots.isEmpty()) {
        CHECK(click(pref(field)), "cannot focus screenshot filter");
        key(settings, Qt::Key_A, Qt::ControlModifier);
        key(settings, Qt::Key_Backspace);
        CHECK(pref(field)->property("text").toString().isEmpty() && QDir().mkpath(shots), "cannot prepare screenshots" << shots);
        for (bool dark : {false, true}) {
            theme->setDarkMode(dark);
            QTest::qWait(100);
            const auto image = settings->grabWindow();
            const auto name = dark ? QStringLiteral("preferences-hotkeys-dark.png") : QStringLiteral("preferences-hotkeys-light.png");
            CHECK(!image.isNull() && image.save(QDir(shots).filePath(name)), "cannot capture" << name);
        }
    }
    settings->close();
    CHECK(focus(window, list), "main did not regain focus after Preferences");
    key(window, Qt::Key_F6);
    key(window, Qt::Key_F7);
    CHECK(!searchVisible() && expectFind(Qt::Key_K, Qt::ControlModifier), "old key fired or edited key failed in the current process");

    auto *search = searchWindow();
    CHECK(focus(search), "Find window did not take focus");
    auto *findField = qmlObject(search, QStringLiteral("findField"));
    CHECK(findField && findField->property("activeFocus").toBool(), "Find window lacks its active input");
    key(search, Qt::Key_O, Qt::AltModifier);
    key(search, Qt::Key_H, Qt::ControlModifier);
    CHECK(!isVisible(settings) && findField->property("activeFocus").toBool(), "a main-window shortcut leaked into Find");
    key(search, Qt::Key_Escape);
    CHECK(QTest::qWaitFor([&] { return !searchVisible(); }, timeout), "Find did not handle its own Escape");

    const auto source = configDir().filePath(QStringLiteral("stage18b.ass"));
    CHECK(writeBytes(source, fixture) && document->openLocalPath(source), document->lastError());
    CHECK(grid->lineCount() == 2 && focus(window, list), "wrong grid fixture or focus");
    grid->selectSingle(0);
    clipboard->clear();
    key(window, Qt::Key_C, Qt::ControlModifier);
    const auto copiedRow = clipboard->text();
    CHECK(copiedRow.startsWith(QStringLiteral("Dialogue:")) && copiedRow.contains(QStringLiteral("first line")),
          "grid Ctrl+C did not copy an ASS row");
    key(window, Qt::Key_X, Qt::ControlModifier);
    CHECK(grid->lineCount() == 1 && document->dirty(), "grid Ctrl+X did not cut the selected row");
    key(window, Qt::Key_V, Qt::ControlModifier);
    CHECK(grid->lineCount() == 2, "grid Ctrl+V did not paste a row");
    key(window, Qt::Key_Z, Qt::ControlModifier);
    key(window, Qt::Key_Z, Qt::ControlModifier);
    CHECK(grid->lineCount() == 2 && !document->dirty(), "clipboard actions were not two undoable steps");

    grid->selectSingle(0);
    CHECK(focus(window, body), "body editor did not take focus");
    key(window, Qt::Key_A, Qt::ControlModifier);
    key(window, Qt::Key_C, Qt::ControlModifier);
    CHECK(clipboard->text() == QStringLiteral("first line"), "grid copy stole native text copy");
    key(window, Qt::Key_X, Qt::ControlModifier);
    CHECK(grid->lineCount() == 2 && body->property("text").toString().isEmpty(), "grid cut stole native text cut");
    clipboard->setText(QStringLiteral("text only"));
    key(window, Qt::Key_V, Qt::ControlModifier);
    CHECK(grid->lineCount() == 2 && body->property("text").toString() == QStringLiteral("text only"), "grid paste stole native text paste");
    const auto imeText = QStringLiteral("\u5b57");
    QInputMethodEvent preedit(imeText, {});
    QCoreApplication::sendEvent(body, &preedit);
    CHECK(body->property("text").toString() == QStringLiteral("text only") &&
          body->property("preeditText").toString() == imeText && grid->lineCount() == 2,
          "IME preedit was swallowed or changed the subtitle prematurely");
    QInputMethodEvent commit;
    commit.setCommitString(imeText);
    QCoreApplication::sendEvent(body, &commit);
    CHECK(body->property("text").toString() == QStringLiteral("text only") + imeText && grid->lineCount() == 2,
          "IME commit did not reach the editor as text");
    auto *pasteOver = qobject_cast<QQuickWindow *>(qmlObject(window, QStringLiteral("pasteOverDialog")));
    CHECK(pasteOver, "production Paste Over window is missing");
    const auto closePasteOver = qScopeGuard([&] { if (pasteOver->isVisible()) pasteOver->close(); });
    key(window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(!isVisible(pasteOver), "Paste Over was triggered from the text editor");
    CHECK(focus(window, list), "grid focus could not be restored for Paste Over");
    clipboard->setText(copiedRow);
    key(window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(QTest::qWaitFor([&] { return isVisible(pasteOver); }, timeout), "grid Ctrl+Shift+V did not open Paste Over");
    key(pasteOver, Qt::Key_Escape);
    CHECK(QTest::qWaitFor([&] { return !isVisible(pasteOver); }, timeout), "Paste Over did not handle Escape");

    CHECK(video->openDummyVideo(640, 360, 25, 200) && focus(window, list), "cannot open dummy video or focus grid");
    video->seekFrame(20);
    key(window, Qt::Key_6, Qt::ControlModifier);
    CHECK(video->currentFrame() == 19, "main-row Ctrl+6 did not take the Default binding");
    key(window, Qt::Key_6, Qt::ControlModifier | Qt::KeypadModifier);
    CHECK(video->currentFrame() == 20, "Ctrl+KP_6 did not take the distinct grid keypad binding");
    key(window, Qt::Key_Return);
    CHECK(video->currentFrame() == 19, "Return did not match the regular Enter binding");
    key(window, Qt::Key_Enter, Qt::KeypadModifier);
    CHECK(video->currentFrame() == 20, "keypad Enter did not match the distinct KP_Enter binding");
    key(window, Qt::Key_S, Qt::ControlModifier);
    CHECK(video->currentFrame() == 19, "Ctrl+S did not match the unshifted binding");
    key(window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(video->currentFrame() == 20, "Ctrl+Shift+S did not match the distinct shifted binding");
    key(window, Qt::Key_8, Qt::ControlModifier);
    CHECK(video->currentFrame() == 19, "a disabled Always binding overrode the grid context");
    key(window, Qt::Key_O, Qt::AltModifier);
    CHECK(QTest::qWaitFor([&] { return isVisible(settings); }, timeout) && focus(settings), "cannot reopen Hotkeys");
    const auto alwaysToggle = QStringLiteral("hotkeyAlwaysEnabled");
    CHECK(QTest::qWaitFor([&] { return isVisible(pref(alwaysToggle)); }, timeout) &&
          !pref(alwaysToggle)->property("checked").toBool() && click(pref(alwaysToggle)) &&
          pref(alwaysToggle)->property("checked").toBool(), "cannot enable Always hotkeys using Preferences");
    settings->close();
    CHECK(focus(window, list), "main did not regain focus after enabling Always");
    key(window, Qt::Key_8, Qt::ControlModifier);
    CHECK(video->currentFrame() == 20, "enabled Always binding did not override the grid context");

    auto *jump = qmlObject(window, QStringLiteral("jumpDialog"));
    CHECK(jump && !isVisible(jump), "video jump dialog missing or already visible");
    key(window, Qt::Key_G, Qt::ControlModifier);
    CHECK(QTest::qWaitFor([&] { return jump->property("opened").toBool(); }, timeout), "Ctrl+G did not open the modal dialog");
    key(window, Qt::Key_8, Qt::ControlModifier);
    key(window, Qt::Key_K, Qt::ControlModifier);
    CHECK(video->currentFrame() == 20 && !searchVisible(), "Main/Always shortcut leaked into a modal popup on the same window");
    key(window, Qt::Key_Escape);
    CHECK(QTest::qWaitFor([&] { return !isVisible(jump); }, timeout), "video jump dialog did not handle its own Escape");

    QProcess child;
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("AEGISUB_STAGE18B_SCREENSHOTS"));
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--stage18b-restart")});
    CHECK(child.waitForStarted(timeout), "cannot start restart verification" << child.errorString());
    CHECK(child.waitForFinished(20000), "restart verification timed out" << child.errorString());
    const auto out = child.readAllStandardOutput();
    const auto err = child.readAllStandardError();
    CHECK(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 && out.contains("Stage 18B restart passed"),
          "restart failed" << child.exitCode() << out << err);
    // Verify reset only after the child has checked the edited map.
    CHECK(focus(window, list), "cannot reactivate the main window after the restart check");
    key(window, Qt::Key_O, Qt::AltModifier);
    CHECK(QTest::qWaitFor([&] { return isVisible(settings); }, timeout) && focus(settings), "cannot reopen Hotkeys to restore defaults");
    const auto restore = QStringLiteral("hotkeyRestoreDefaults");
    const auto confirm = QStringLiteral("hotkeyConfirmRestoreDefaults");
    CHECK(QTest::qWaitFor([&] { return isVisible(pref(restore)); }, timeout) && click(pref(restore)) &&
          QTest::qWaitFor([&] { return isVisible(pref(confirm)); }, timeout) && click(pref(confirm)),
          "cannot confirm Restore Defaults in Preferences");
    CHECK(QTest::qWaitFor([&] { return menuLabel(window, findId) == QStringLiteral("Ctrl+F"); }, timeout) &&
          keys(readBindings(), "Default", findId) == QJsonArray{QStringLiteral("Ctrl-F")} &&
          menuLabel(window, "subtitle/save").contains(QStringLiteral("Ctrl+S")) &&
          menuLabel(window, "subtitle/save/as") == QStringLiteral("Ctrl+Shift+S"),
          "Restore Defaults did not restore the Find/Save command labels and disk assignment");
    settings->close();
    CHECK(focus(window, list), "main did not regain focus after Restore Defaults");
    key(window, Qt::Key_K, Qt::ControlModifier);
    CHECK(!searchVisible() && expectFind(Qt::Key_F, Qt::ControlModifier), "reset did not replace the edited key with built-in Ctrl+F");
    qInfo() << "Stage 18B passed";
    return 0;
}
