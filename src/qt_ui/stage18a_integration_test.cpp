#include "../ass_dialogue.h"
#include "viewmodels/preferences_vm.h"
#include "automation/automation_vm.h"
#include "stage17a_attachments_test.h"
#include "viewmodels/audio_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/theme_vm.h"
#include "viewmodels/video_vm.h"

#include <QFont>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <array>
#include <functional>

namespace {
using namespace stage17a_attachments_test;
using aegisub_qt::automation::AutomationViewModel;
#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 18A:" << __LINE__ << message; return 1; \
} } while (false)

constexpr auto incKey = "Path/Automation/Include";
constexpr auto autoKey = "Path/Automation/Autoload";
constexpr auto traceKey = "Automation/Trace Level";
constexpr auto warnKey = "Subtitle/Character Counter/CPS Warning Threshold";
constexpr auto errorKey = "Subtitle/Character Counter/CPS Error Threshold";
constexpr auto spaceKey = "Subtitle/Character Counter/Ignore Whitespace";
constexpr auto punctKey = "Subtitle/Character Counter/Ignore Punctuation";
constexpr auto editFontKey = "Subtitle/Edit Box/Font Face";
constexpr auto gridFontKey = "Subtitle/Grid/Font Face";
constexpr auto leadInKey = "Audio/Lead/IN";
constexpr auto leadOutKey = "Audio/Lead/OUT";
constexpr auto jumpKey = "Video/Slider/Fast Jump Step";
constexpr auto fixture = R"ASS([Script Info]
ScriptType: v4.00+
PlayResX: 640
PlayResY: 360
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.10,0:00:01.10,Default,,0,0,0,,{\b1}abcdefgh, ijklmnop!
Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,Another line
Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,Not selected
)ASS";

QQuickItem *control(QQuickWindow *window, const char *name) {
    return window ? visualItem(window->contentItem(), QString::fromLatin1(name)) : nullptr;
}

QVariant configValue(const PreferencesViewModel &prefs, const char *key) {
    QFile file(prefs.configFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonValue value(QJsonDocument::fromJson(file.readAll()).object());
    for (const auto &part : QString::fromLatin1(key).split('/')) value = value.toObject().value(part);
    return value.toVariant();
}

struct Cover { const char *group; const char *role; const char *key; const char *hex; const char *rgb; };
constexpr std::array covers{
    Cover{"audio", "Play Cursor", "Colour/Audio Display/Play Cursor", "#1e82c4", "rgb(30, 130, 196)"},
    Cover{"syntax", "Normal", "Colour/Subtitle/Syntax/Normal", "#25745e", "rgb(37, 116, 94)"},
    Cover{"grid", "CPS Error", "Colour/Subtitle Grid/CPS Error", "#d82b45", "rgb(216, 43, 69)"},
    Cover{"visual", "Lines Primary", "Colour/Visual Tools/Lines Primary", "#8c39d1", "rgb(140, 57, 209)"},
};

QVariantMap findMacro(const QVariantList &nodes, const QString &title) {
    for (const auto &entry : nodes) {
        const auto node = entry.toMap();
        if (node.value("title").toString() == title && node.contains("id")) return node;
        const auto child = findMacro(node.value("children").toList(), title);
        if (!child.isEmpty()) return child;
    }
    return {};
}

QString runTrace(AutomationViewModel &vm) {
    vm.validateMenu();
    const auto macro = findMacro(vm.menu(), QStringLiteral("Stage18 trace"));
    QSignalSpy finished(&vm, &AutomationViewModel::operationFinished);
    if (macro.isEmpty() || !vm.runMacro(macro.value("id").toInt())) return {};
    if (finished.isEmpty() && !finished.wait(timeout)) return {};
    return finished.first().first().toBool() ? vm.progressLog() : QString{};
}

QObject *menuItem(QQuickWindow *window, const char *name) {
    auto *bar = object(window->property("menuBar"));
    if (!bar) return nullptr;
    for (int i = 0; i < bar->property("count").toInt(); ++i) {
        auto *menu = qmlObject(bar, QStringLiteral("menuAt(%1)").arg(i));
        if (!menu) continue;
        for (int j = 0; j < menu->property("count").toInt(); ++j) {
            auto *item = qmlObject(menu, QStringLiteral("itemAt(%1)").arg(j));
            if (item && item->objectName() == QLatin1String(name)) return item;
        }
    }
    return nullptr;
}

bool capture(QQuickWindow &window, const QDir &dir, const QString &name) {
    QTest::qWait(100);
    const auto image = window.grabWindow();
    return !image.isNull() && image.save(dir.filePath(name + QStringLiteral(".png")));
}
}

int runStage18AIntegrationTest(QQmlApplicationEngine &engine, bool restart) {
    const auto vm = [&](const char *name) { return object(engine.rootContext()->contextProperty(QLatin1String(name))); };
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    auto *prefs = qobject_cast<PreferencesViewModel *>(vm("preferencesVM"));
    auto *theme = qobject_cast<ThemeViewModel *>(vm("themeVM"));
    auto *grid = qobject_cast<SubtitleGridModel *>(vm("gridModel"));
    auto *document = qobject_cast<DocumentViewModel *>(vm("documentVM"));
    auto *undo = qobject_cast<UndoManager *>(vm("undoVM"));
    auto *video = qobject_cast<VideoViewModel *>(vm("videoVM"));
    auto *audio = qobject_cast<AudioViewModel *>(vm("audioVM"));
    auto *automation = qobject_cast<AutomationViewModel *>(vm("automationVM"));
    CHECK(window && prefs && theme && grid && document && undo && video && audio && automation, "missing production VMs");
    const auto option = [&](const char *key) { return prefs->optionValues().value(QString::fromLatin1(key)); };
    const auto set = [&](const char *key, const QVariant &value) { return prefs->setOption(QString::fromLatin1(key), value); };
    const auto clean = [&] { return !document->dirty() && !undo->canUndo(); };
    if (restart) {
        CHECK(option(traceKey).toInt() == 5 && option(editFontKey).toString() == "Arial" && option(jumpKey).toInt() == 17,
              "settings did not survive restart");
        for (const auto &entry : covers) {
            CHECK(prefs->colorOverridesEnabled(QString::fromLatin1(entry.group)) == (QLatin1String(entry.group) != "grid") &&
                  configValue(*prefs, entry.key).toString() == QLatin1String(entry.rgb), entry.group << "override did not survive restart");
        }
        CHECK(!findMacro(automation->menu(), QStringLiteral("Stage18 trace")).isEmpty() && clean(), "autoload or clean state after restart");
        qInfo() << "Stage 18A restart passed";
        return 0;
    }

    CHECK(option(leadInKey).toInt() == 100 && option(leadOutKey).toInt() == 350 && option(jumpKey).toInt() == 10 &&
          option(traceKey).toInt() == 3 && option(warnKey).toInt() == 15 && option(errorKey).toInt() == 30 &&
          option(spaceKey).toBool() && option(punctKey).toBool() && option(editFontKey).toString().isEmpty() &&
          option(gridFontKey).toString() == "Tahoma", "original defaults not loaded");
    const QDir files(QFileInfo(prefs->configFilePath()).absolutePath());
    for (const auto *dir : {"include-first", "include-second", "autoload-first", "autoload-second"})
        CHECK(files.mkdir(QString::fromLatin1(dir)), "cannot create fixture directory" << dir);
    CHECK(writeBytes(files.filePath("include-first/stage18_order.lua"), "return 'first'\n") &&
          writeBytes(files.filePath("include-second/stage18_order.lua"), "return 'second'\n") &&
          writeBytes(files.filePath("include-second/stage18_other.lua"), "return 'other'\n") &&
          writeBytes(files.filePath("autoload-first/trace.lua"), R"LUA(
local order = require('stage18_order')
local other = require('stage18_other')
aegisub.register_macro('Stage18 trace', '', function()
  aegisub.log('stage18-plain-%s-%s', order, other)
  aegisub.log(0, 'stage18-fatal')
  aegisub.log(3, 'stage18-hint')
  aegisub.debug.out(5, 'stage18-deep')
end)
)LUA") && writeBytes(files.filePath("autoload-second/second.lua"),
          "aegisub.register_macro('Stage18 second directory', '', function() end)\n"), "cannot write Lua fixtures");
    const auto firstInclude = files.absoluteFilePath("include-first");
    const auto secondInclude = files.absoluteFilePath("include-second");
    const auto include = firstInclude + u'|' + secondInclude;
    const auto autoload = files.absoluteFilePath("autoload-first") + u'|' + files.absoluteFilePath("autoload-second");
    window->requestActivate();
    QTest::qWait(50);
    QTest::keyClick(window, Qt::Key_O, Qt::AltModifier);
    QTest::qWait(80);
    auto *settings = qobject_cast<QQuickWindow *>(object(window->property("preferencesWindow")));
    CHECK(settings && settings->isVisible(), "cannot open production Preferences");
    prefs->setPreferencePage(7);
    QCoreApplication::processEvents();
    const auto enter = [](QQuickItem *field, const QString &text) {
        return field && field->setProperty("text", text) && QMetaObject::invokeMethod(field, "editingFinished");
    };
    auto *trace = control(settings, traceKey);
    CHECK(trace && trace->property("count").toInt() == 6 && enter(control(settings, incKey), include) &&
          enter(control(settings, autoKey), autoload) && QMetaObject::invokeMethod(trace, "activated", Q_ARG(int, 0)) &&
          option(incKey).toString() == include && option(autoKey).toString() == autoload && option(traceKey).toInt() == 0,
          "Automation settings UI did not publish values");
    CHECK(findMacro(automation->menu(), QStringLiteral("Stage18 trace")).isEmpty(), "save triggered autoload");
    automation->reloadAutoload();
    CHECK(!findMacro(automation->menu(), QStringLiteral("Stage18 second directory")).isEmpty(), "second autoload path not scanned" << automation->scripts());
    const auto quiet = runTrace(*automation);
    CHECK(quiet.contains("stage18-plain-first-other") && quiet.contains("stage18-fatal") &&
          !quiet.contains("stage18-hint") && !quiet.contains("stage18-deep"), "wrong trace level 0" << quiet);
    CHECK(set(traceKey, 5) && set(incKey, secondInclude + u'|' + firstInclude), "cannot update Automation settings");
    int row = 0;
    const auto scripts = automation->scripts();
    while (row < scripts.size() && !scripts[row].toMap().value("path").toString().endsWith("trace.lua")) ++row;
    CHECK(row < scripts.size(), "autoload script missing" << scripts);
    automation->reloadScript(row);
    const auto verbose = runTrace(*automation);
    CHECK(verbose.contains("stage18-plain-second-other") && verbose.contains("stage18-hint") &&
          verbose.contains("stage18-deep"), "reload or trace level 5 not applied" << verbose);
    settings->close();

    const auto source = files.filePath("stage18a.ass");
    CHECK(writeBytes(source, fixture) && document->openLocalPath(source), document->lastError());
    window->requestActivate();
    QTest::qWait(80);
    grid->selectSingle(0);
    grid->toggleSelect(1);
    auto times = [&] (int index) {
        const auto *row = grid->dialogueAt(index);
        return std::pair(int(row->Start), int(row->End));
    };
    CHECK(set(leadInKey, 250) && set(leadOutKey, 750), "cannot set lead values");
    auto *leadIn = menuItem(window, "addLeadInMenuItem");
    auto *leadOut = menuItem(window, "addLeadOutMenuItem");
    CHECK(leadIn && leadOut && leadIn->property("enabled").toBool() && leadOut->property("enabled").toBool(), "Timing lead commands missing");
    CHECK(QMetaObject::invokeMethod(leadIn, "triggered") && times(0).first == 0 && times(1).first == 1750 &&
          times(2).first == 4000 && document->dirty() && undo->canUndo(), "lead-in did not adjust selected starts");
    undo->undo();
    CHECK(times(0).first == 100 && times(1).first == 2000 && clean(), "lead-in undo was not one step");
    CHECK(QMetaObject::invokeMethod(leadOut, "triggered") && times(0).second == 1850 && times(1).second == 3750 &&
          times(2).second == 5000 && document->dirty() && undo->canUndo(), "lead-out did not adjust selected ends");
    undo->undo();
    CHECK(times(0).second == 1100 && times(1).second == 3000 && clean(), "lead-out undo was not one step");
    CHECK(set(jumpKey, 17) && video->openDummyVideo(640, 360, 25, 200), "cannot set step or open video");
    auto *list = control(window, "subtitleGridList");
    auto *body = control(window, "editBoxTextArea");
    CHECK(list && body, "missing grid and EditBox");
    list->forceActiveFocus();
    const int frame = video->currentFrame();
    QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
    CHECK(video->currentFrame() == frame + 17, "Alt+Right did not fast jump");
    QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier);
    CHECK(video->currentFrame() == frame, "Alt+Left did not fast jump");
    body->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
    CHECK(video->currentFrame() == frame, "fast jump stole a key from EditBox");

    grid->selectSingle(0);
    const auto cpsRole = role(grid, "cps");
    auto cps = [&] { return grid->data(grid->index(0, 0), cpsRole).toInt(); };
    auto *count = control(window, "editBoxCharacterCount");
    auto *gridText = control(window, "subtitleGridText");
    auto *cpsBg = control(window, "subtitleGridCpsBackground");
    CHECK(cpsRole >= 0 && count && gridText && cpsBg && cps() == 16 && count->property("count").toInt() == 16,
          "missing CPS/counter baseline");
    CHECK(set(editFontKey, QStringLiteral("Arial")) && set(gridFontKey, QStringLiteral("Consolas")) &&
          body->property("font").value<QFont>().family() == "Arial" && gridText->property("font").value<QFont>().family() == "Consolas",
          "font faces did not apply immediately");
    CHECK(set(gridFontKey, QStringLiteral("")) && gridText->property("font").value<QFont>().family() == "Tahoma", "empty grid font fallback");
    CHECK(set(spaceKey, false) && cps() == 17 && count->property("count").toInt() == 17 &&
          set(punctKey, false) && cps() == 19 && count->property("count").toInt() == 19,
          "ignore settings did not update both counters");
    const auto color = [](QObject *obj) { return obj->property("color").value<QColor>(); };
    CHECK(set(warnKey, 19) && set(errorKey, 16), "cannot set CPS thresholds");
    const auto fraction = [&] { return cpsBg->opacity(); };
    const auto noWarning = fraction();
    CHECK(set(warnKey, 15) && set(errorKey, 22), "cannot set mixed CPS thresholds");
    const auto mixed = fraction();
    CHECK(set(errorKey, 19), "cannot set full CPS warning");
    const auto full = fraction();
    CHECK(noWarning == 0 && mixed > 0 && mixed < 1 && full == 1 && set(errorKey, 10) && fraction() == full,
          "CPS warning ramp or below-warning clamp failed" << noWarning << mixed << full);
    CHECK(audio->openDummyAudio(), "cannot open dummy waveform");
    QTest::qWait(80);
    auto *waveform = control(window, "audioWaveform");
    auto *overlay = control(window, "visualOverlay");
    CHECK(waveform && overlay, "missing media paint consumers");
    const std::array<std::function<QColor()>, 4> paint{
        [&] { return waveform->property("colors").toMap().value("playhead").value<QColor>(); },
        [&] { return color(body); }, [&] { return color(cpsBg); },
        [&] { return overlay->property("accentColor").value<QColor>(); },
    };
    for (size_t i = 0; i < covers.size(); ++i) {
        const auto &entry = covers[i];
        const auto group = QString::fromLatin1(entry.group);
        const auto key = QString::fromLatin1(entry.role);
        const QColor overrideColor(QString::fromLatin1(entry.hex));
        const auto baseline = paint[i]();
        CHECK(!prefs->colorOverridesEnabled(group), "override unexpectedly on by default" << group);
        prefs->setColorOverridesEnabled(group, true);
        CHECK(prefs->setColorOverride(group, key, overrideColor), "cannot save original color" << group);
        QCoreApplication::processEvents();
        CHECK(paint[i]() == overrideColor && configValue(*prefs, entry.key).toString() == QLatin1String(entry.rgb) && clean(),
              "color did not reach item and disk or dirtied document" << group << paint[i]() << overrideColor);
        prefs->setColorOverridesEnabled(group, false);
        QCoreApplication::processEvents();
        CHECK(paint[i]() == baseline && configValue(*prefs, entry.key).toString() == QLatin1String(entry.rgb) && clean(),
              "disabling lost override or dirtied document" << group);
    }
    CHECK(set(errorKey, 22), "cannot leave grid in mixed warning mode");
    const auto shots = qEnvironmentVariable("AEGISUB_STAGE18A_SCREENSHOTS");
    if (!shots.isEmpty()) {
        CHECK(QDir().mkpath(shots), "cannot create PNG output directory");
        const QDir dir(shots);
        window->resize(1180, 780);
        video->closeVideo();
        audio->closeAudio();
        settings->resize(840, 700);
        constexpr std::array pages{std::pair{7, "automation"}, std::pair{8, "colors"}, std::pair{4, "interface"}};
        for (bool dark : {false, true}) {
            theme->setDarkMode(dark);
            const auto suffix = dark ? QStringLiteral("-dark") : QStringLiteral("-light");
            CHECK(capture(*window, dir, QStringLiteral("cps-grid") + suffix), "cannot capture grid");
            settings->show();
            settings->requestActivate();
            for (const auto &[index, name] : pages) {
                prefs->setPreferencePage(index);
                CHECK(capture(*settings, dir, QStringLiteral("preferences-") + QString::fromLatin1(name) + suffix),
                      "cannot capture Preferences" << name);
            }
            settings->close();
        }
    }
    for (const auto &entry : covers)
        prefs->setColorOverridesEnabled(QString::fromLatin1(entry.group), QLatin1String(entry.group) != "grid");
    CHECK(clean(), "preferences dirtied document");
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--stage18a-restart")});
    CHECK(child.waitForFinished(20000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 &&
          child.readAllStandardOutput().contains("Stage 18A restart passed"), "restart failed" << child.readAllStandardError());
    qInfo() << "Stage 18A passed";
    return 0;
}
