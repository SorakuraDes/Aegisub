#include "viewmodels/subtitle_grid_model.h" // agi::signal before Qt macros
#include "automation/automation_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/undo_manager.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJSValue>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#ifdef Q_OS_WIN
#include <QScopeGuard>
#include <qt_windows.h>
#endif

namespace {
using aegisub_qt::automation::AutomationViewModel;
constexpr int timeout = 5000;
const auto english = QStringLiteral("en_US");
const auto chinese = QStringLiteral("zh_CN");

#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 16B integration:" << __LINE__ << message; return 1; \
} } while (false)

QObject *object(const QVariant &value) {
    if (auto *obj = value.value<QObject *>()) return obj;
    return value.value<QJSValue>().toQObject();
}

template<class T>
T *vm(QQmlApplicationEngine &engine, const char *key) {
    return qobject_cast<T *>(object(engine.rootContext()->contextProperty(QLatin1String(key))));
}

QVariant evaluateValue(QObject *scope, const QString &code) {
    if (!scope || !qmlContext(scope)) return {};
    QQmlExpression expression(qmlContext(scope), scope, code);
    return expression.evaluate();
}

QObject *evaluate(QObject *scope, const QString &code) {
    return object(evaluateValue(scope, code));
}

QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *match = visualChild(child, name)) return match;
    return nullptr;
}

QObject *named(QObject *owner, const QString &name) {
    if (!owner) return nullptr;
    if (auto *child = owner->findChild<QObject *>(name)) return child;
    if (auto *item = qobject_cast<QQuickItem *>(owner)) return visualChild(item, name);
    if (auto *window = qobject_cast<QQuickWindow *>(owner))
        if (auto *match = visualChild(window->contentItem(), name)) return match;
    for (const char *section : {"contentItem", "footer", "header"})
        if (auto *match = visualChild(qobject_cast<QQuickItem *>(object(owner->property(section))), name)) return match;
    auto *candidate = evaluate(owner, name); // Main.qml IDs can outlive visual reparenting.
    return candidate && candidate->objectName() == name ? candidate : nullptr;
}

QString text(QObject *control, const char *property = "text") {
    return control ? control->property(property).toString() : QString{};
}

bool flag(QObject *control, const char *property) {
    return control && control->property(property).toBool();
}

bool set(QObject *control, const char *property, const QVariant &value) {
    return control && control->setProperty(property, value);
}

bool click(QObject *control) {
    auto *item = qobject_cast<QQuickItem *>(control);
    if (!item || !item->window() || !item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0)
        return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, point);
    return true;
}

bool waitOpened(QObject *dialog) {
    return dialog && QTest::qWaitFor([&] { return flag(dialog, "opened"); }, timeout);
}

bool waitClosed(QObject *dialog) {
    return dialog && QTest::qWaitFor([&] { return !flag(dialog, "visible"); }, timeout);
}

bool finish(AutomationViewModel *automation, QSignalSpy &finished, bool expected) {
    const bool ok = QTest::qWaitFor([&] { return !automation->busy() && !finished.isEmpty(); }, timeout) &&
                    finished.size() == 1 && finished.first().value(0).toBool() == expected;
    if (ok) finished.clear();
    return ok;
}

bool capture(QQuickWindow *window, const QString &dir, const QString &name) {
    if (dir.isEmpty()) return true;
    QTest::qWait(100);
    const auto path = QDir(dir).filePath(name);
    if (!window->grabWindow().save(path)) return false;
    qInfo().noquote() << "Stage 16B screenshot:" << path;
    return true;
}

bool captureBoth(QQuickWindow *window, TranslationService *i18n, const QString &dir, const QString &prefix,
                 QObject *localized, const char *property, const QString &message) {
    const auto original = i18n->t(message);
    const auto matches = [&] { return text(localized, property) == i18n->t(message); };
    return QTest::qWaitFor(matches, timeout) && capture(window, dir, prefix + QStringLiteral("-en.png")) &&
           i18n->setLanguage(chinese) && i18n->t(message) != original && QTest::qWaitFor(matches, timeout) &&
           capture(window, dir, prefix + QStringLiteral("-zh.png")) && i18n->setLanguage(english) &&
           QTest::qWaitFor(matches, timeout);
}

QVariantMap entry(const QVariantList &entries, const QString &title, const QString &key = QStringLiteral("title")) {
    for (const auto &value : entries) {
        const auto map = value.toMap();
        if (map.value(key) == title) return map;
        if (auto child = entry(map.value(QStringLiteral("children")).toList(), title, key); !child.isEmpty()) return child;
    }
    return {};
}

bool run(AutomationViewModel *automation, const QString &name) {
    bool idOk = false;
    const int id = entry(automation->menu(), name).value(QStringLiteral("id")).toInt(&idOk);
    return idOk && !automation->busy() && automation->runMacro(id);
}

QObject *menuItem(QObject *menu, const QString &name) {
    if (!menu) return nullptr;
    for (int i = 0; i < menu->property("count").toInt(); ++i) {
        auto *item = evaluate(menu, QStringLiteral("itemAt(%1)").arg(i));
        if (!item) continue;
        if (item->objectName() == name) return item;
        if (auto *child = menuItem(object(item->property("subMenu")), name)) return child;
    }
    return nullptr;
}

QObject *topMenu(QQuickWindow *window, const QString &name) {
    auto *bar = object(window->property("menuBar"));
    if (!bar) return nullptr;
    for (int i = 0; i < bar->property("count").toInt(); ++i) {
        auto *menu = evaluate(bar, QStringLiteral("menuAt(%1)").arg(i));
        if (menu && menu->objectName() == name) return menu;
    }
    return nullptr;
}

bool openMenu(QObject *menu) {
    if (!menu || !qmlContext(menu)) return false;
    QQmlExpression expression(qmlContext(menu), menu, QStringLiteral("popup(450, 25)"));
    expression.evaluate();
    return !expression.hasError() && waitOpened(menu);
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size() && file.flush();
}

// Exercise the running Qt picker rather than injecting its cached result.
bool selectAndAccept(QObject *dialog, const QUrl &url, bool multiple = false) {
    if (!flag(dialog, "visible")) return false;
    QQuickItem *item = nullptr;
    const auto folder = QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath());
    set(dialog, "currentFolder", folder);
    if (!QTest::qWaitFor([&] {
        for (auto *window : QGuiApplication::allWindows()) {
            auto *quick = qobject_cast<QQuickWindow *>(window);
            if (!quick) continue;
            auto *list = visualChild(quick->contentItem(), QStringLiteral("fileDialogListView"));
            if (!list || !list->isVisible()) continue;
            for (int i = 0; i < list->property("count").toInt(); ++i) {
                auto *row = visualChild(list, QStringLiteral("fileDialogDelegate%1").arg(i));
                if (row && row->property("file").toUrl() == url) { item = row; return true; }
            }
            if (!multiple) {
                auto *field = visualChild(quick->contentItem(), QStringLiteral("fileNameTextField"));
                if (field && field->isVisible()) { item = field; return true; }
            }
        }
        return false;
    }, 1500)) return false;
    if (!click(item)) return false;
    if (item->objectName() == QStringLiteral("fileNameTextField"))
        item->setProperty("text", QFileInfo(url.toLocalFile()).fileName());
    QTest::keyClick(item->window(), Qt::Key_Return);
    return QTest::qWaitFor([&] { return !flag(dialog, "visible"); }, 1500);
}

constexpr auto assFixture = R"ass([Script Info]
ScriptType: v4.00+
PlayResX: 320
PlayResY: 180

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\k100}Alpha
Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,{\k100}Beta
Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,template line notext,Stage16B-Templated
)ass";

constexpr auto luaFixture = R"lua(script_name = "Stage 16B integration fixture"
script_description = "QML dialogs, cancellation and configured export"
script_author = "Stage 16B"
script_version = "1"

local function edit(subs, sel, value, label)
    local line = subs[sel[1]]
    line.text = value
    subs[sel[1]] = line
    aegisub.set_undo_point(label)
end

local fields = {
    {class="label", name="caption", x=0,y=0,width=2, label="Stage 16B: ten Lua form controls"},
    {class="edit", name="single", x=0,y=1,width=2, text="Initial edit"},
    {class="textbox", name="multi", x=0,y=2,width=2, text="Initial textbox"},
    {class="intedit", name="integer", x=0,y=3, value=2,min=0,max=9},
    {class="floatedit", name="decimal", x=1,y=3, value=1.25,min=0,max=10,step=0.25},
    {class="dropdown", name="choice", x=0,y=4, items={"First","Second"},value="First"},
    {class="checkbox", name="toggle", x=1,y=4, label="Include text",value=false},
    {class="color", name="rgb", x=0,y=5, value="#112233"},
    {class="coloralpha", name="rgba", x=1,y=5, value="#44556677"},
    {class="alpha", name="alpha", x=0,y=6,width=2, value="#11"}
}

aegisub.register_macro("Stage 16B/Fields", "Round-trip every control", function(subs, sel)
    local button, v = aegisub.dialog.display(fields, {"Apply fixture", "Abort fixture"},
                                             {ok="Apply fixture", cancel="Abort fixture"})
    assert(button == "Apply fixture", tostring(button))
    assert(v.caption == nil)
    assert(v.single == "Edited text" and v.multi == "First line\nSecond line")
    assert(v.integer == 7 and math.abs(v.decimal - 2.75) < 0.0001)
    assert(v.choice == "Second" and v.toggle == true)
    assert(v.rgb:upper() == "#123456" and v.rgba:upper() == "#65432180" and v.alpha == "#AC")
    edit(subs, sel, "Stage16B-Form", "Stage 16B form result")
end)

aegisub.register_macro("Stage 16B/Color alpha", "ASS alpha endpoints and invalid default", function()
    local button, v = aegisub.dialog.display({
        {class="coloralpha", name="opaque", x=0,y=0, value="#12345600"},
        {class="coloralpha", name="transparent", x=1,y=0, value="#ABCDEFff"},
        {class="coloralpha", name="invalid", x=0,y=1, value="not-a-color"}
    })
    assert(button == "")
    assert(v.opaque == "#123456FF", tostring(v.opaque))
    assert(v.transparent == "#ABCDEF00", tostring(v.transparent))
    assert(v.invalid == "#00000000", tostring(v.invalid))
end)

aegisub.register_macro("Stage 16B/Confirm", "Separate confirmation and rollback fixture", function(subs, sel)
    edit(subs, sel, "must not survive", "must not survive")
    local button = aegisub.dialog.display({{class="label", name="confirm",label="Apply the fixture change?"}})
    if button == false then aegisub.cancel() end
    assert(button == "")
    edit(subs, sel, "Stage16B-Confirmed", "Stage 16B confirmation")
end)

aegisub.register_macro("Stage 16B/Progress", "Progress and transaction cancellation", function(subs, sel)
    edit(subs, sel, "progress must roll back", "must not survive")
    aegisub.progress.title("Stage 16B progress")
    aegisub.progress.task("Waiting for cancellation")
    aegisub.progress.set(42)
    aegisub.debug.out(0, "stage16b progress log\n")
    local deadline = os.clock() + 15
    while os.clock() < deadline do
        if aegisub.progress.is_cancelled() then aegisub.cancel() end
    end
    error("Timed out waiting for the progress Cancel button")
end)

aegisub.register_macro("Stage 16B/File", "Use the real Automation file picker", function(subs, sel)
    local paths = aegisub.dialog.open("Stage 16B pick text", "", "", "Text files|*.txt", true, true)
    assert(type(paths) == "table" and #paths == 1)
    local f = assert(io.open(paths[1], "rb"))
    local payload = f:read("*a")
    f:close()
    edit(subs, sel, payload, "Stage 16B file result")
end)

aegisub.register_macro("Stage 16B/Single open", "Check the single path return", function(subs, sel)
    local path = aegisub.dialog.open("Single open", "", "", "Text|*.txt;*.md|All|*.*", false)
    assert(type(path) == "string" and path:match("stage16b%-input%.txt$"))
    edit(subs, sel, "Single open ok", "Single open")
end)

aegisub.register_macro("Stage 16B/Save path", "Check the Unicode save path return", function(subs, sel)
    local path = aegisub.dialog.save("Unicode save", "", "", "Text|*.txt", true)
    assert(type(path) == "string" and path:find("保存文件.txt", 1, true))
    edit(subs, sel, "Save path ok", "Save path")
end)

aegisub.register_macro("Stage 16B/File cancel", "Reject the actual Automation picker", function()
    local path = aegisub.dialog.open("Stage 16B cancel picker", "", "", "Text files|*.txt", false, true)
    assert(path == nil)
    aegisub.cancel()
end)

local function filter(subs, config)
    assert(config.prefix == "Configured-", tostring(config.prefix))
    local accent = "\195\169" -- UTF-8 source text, expected to be transcoded at export.
    local hits = 0
    for i=1,#subs do
        local line = subs[i]
        if line.class == "dialogue" and not line.comment and line.effect == "fx" then
            line.text = config.prefix .. line.text .. accent
            subs[i] = line
            hits = hits + 1
        end
    end
    assert(hits == 2, "Karaoke template must run before the configured filter; hits=" .. hits)
end

aegisub.register_filter("Stage 16B configured", "Prefix Karaoke template results", 3000, filter, function()
    return {{class="edit", name="prefix",text="Unconfigured-"}}
end)
)lua";
}

int runStage16BIntegrationTest(QQmlApplicationEngine &engine) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    auto *automation = vm<AutomationViewModel>(engine, "automationVM");
    auto *document = vm<DocumentViewModel>(engine, "documentVM");
    auto *grid = vm<SubtitleGridModel>(engine, "gridModel");
    auto *undo = vm<UndoManager>(engine, "undoVM");
    auto *i18n = vm<TranslationService>(engine, "i18n");
    CHECK(window && window->isVisible() && automation && document && grid && undo && i18n,
          "missing production QML window or ViewModel");
    CHECK(i18n->setLanguage(english), i18n->error());

    const auto artifacts = qEnvironmentVariable("AEGISUB_QT_STAGE16B_ARTIFACTS");
    CHECK(artifacts.isEmpty() || QFileInfo(artifacts).isDir(), "artifact directory does not exist" << artifacts);
    QTemporaryDir temp;
    CHECK(temp.isValid(), temp.errorString());
    const QDir files(temp.path());
    const auto ass = files.filePath(QStringLiteral("stage16b.ass"));
    const auto script = files.filePath(QStringLiteral("stage16b.lua"));
    const auto input = files.filePath(QStringLiteral("stage16b-input.txt"));
    const auto output = files.filePath(QStringLiteral("stage16b-export.ass"));
    CHECK(writeFile(ass, assFixture) && writeFile(script, luaFixture) && writeFile(input, "Stage16B-File"),
          "cannot write Stage 16B fixtures");
    CHECK(document->openLocalPath(ass) && grid->rowCount() == 3, document->lastError());
    grid->selectSingle(0);
    automation->addScripts({QUrl::fromLocalFile(script)});
    QSignalSpy finished(automation, &AutomationViewModel::operationFinished);
    QSignalSpy forms(automation, &AutomationViewModel::dialogRequested);
    QSignalSpy picks(automation, &AutomationViewModel::fileRequested);
    QSignalSpy errors(automation, &AutomationViewModel::executionError);
    CHECK(finished.isValid() && forms.isValid() && picks.isValid() && errors.isValid(), "missing operation signals");

    auto *dialog = named(window, QStringLiteral("luaDialog"));
    auto *progress = named(window, QStringLiteral("automationProgressDialog"));
    auto *picker = named(window, QStringLiteral("automationFileDialog"));
    auto *exportDialog = named(window, QStringLiteral("exportSubtitlesDialog"));
    CHECK(dialog && progress && picker && exportDialog, "missing production dialogs" << dialog << progress << picker << exportDialog);
    auto row = [&](int index, int role = SubtitleGridModel::TextRole) { return grid->data(grid->index(index, 0), role).toString(); };
    const auto alpha = row(0);
    const auto beta = row(1);
    const auto betaStart = row(1, SubtitleGridModel::StartTimeRole);
    auto clean = [&] {
        return grid->rowCount() == 3 && row(0) == alpha && row(1) == beta && row(1, SubtitleGridModel::StartTimeRole) == betaStart &&
               !document->dirty() && !undo->canUndo();
    };
    auto control = [&](const QString &name) { return named(dialog, QStringLiteral("luaControl_") + name); };
    auto button = [&](int index) { return named(dialog, QStringLiteral("luaButton_%1").arg(index)); };

    CHECK(run(automation, QStringLiteral("Fields")) && waitOpened(dialog) && automation->busy() && finished.isEmpty() && forms.size() == 1,
          "opening the real Lua form did not suspend the operation" << errors);
    const auto request = forms.takeFirst().value(0).toMap();
    CHECK(request.value(QStringLiteral("controls")).toList().size() == 10 && named(dialog, QStringLiteral("luaForm")),
          "expected all ten controls in the real Lua form");
    CHECK(text(control(QStringLiteral("caption"))) == QStringLiteral("Stage 16B: ten Lua form controls") &&
          text(control(QStringLiteral("single"))) == QStringLiteral("Initial edit") &&
          text(control(QStringLiteral("multi"))) == QStringLiteral("Initial textbox") &&
          text(control(QStringLiteral("choice")), "currentText") == QStringLiteral("First") &&
          !flag(control(QStringLiteral("toggle")), "checked"), "Lua control defaults were not rendered");
    CHECK(set(control(QStringLiteral("single")), "text", QStringLiteral("Edited text")) &&
          set(control(QStringLiteral("multi")), "text", QStringLiteral("First line\nSecond line")) &&
          set(control(QStringLiteral("integer")), "value", 7) &&
          set(control(QStringLiteral("decimal")), "text", QLocale().toString(2.75, 'f', 2)) &&
          set(control(QStringLiteral("choice")), "currentIndex", 1) &&
          click(control(QStringLiteral("toggle"))) && flag(control(QStringLiteral("toggle")), "checked") &&
          set(control(QStringLiteral("rgb")), "selectedColor", QColor(0x12, 0x34, 0x56)) &&
          flag(control(QStringLiteral("rgba")), "showAlpha") &&
          set(control(QStringLiteral("rgba")), "selectedColor", QColor(0x65, 0x43, 0x21, 0x7f)) &&
          set(control(QStringLiteral("alpha")), "text", QStringLiteral("&Hac&")), "could not edit the production Lua form");
    CHECK(captureBoth(window, i18n, artifacts, QStringLiteral("stage16b-lua-form"), dialog, "title", QStringLiteral("Automation")),
          "Lua form failed to retranslate or capture in both locales");
    CHECK(text(button(0)) == QStringLiteral("Apply fixture") && text(button(1)) == QStringLiteral("Abort fixture") &&
          flag(button(0), "highlighted") && click(button(0)) && finish(automation, finished, true) && waitClosed(dialog) &&
          row(0) == QStringLiteral("Stage16B-Form") && document->dirty() && undo->undoDescription() == QStringLiteral("Stage 16B form result"),
          "custom Apply button did not return all edited values and commit once" << errors);
    undo->undo();
    CHECK(clean() && undo->canRedo(), "form undo did not restore the original document");

    CHECK(run(automation, QStringLiteral("Color alpha")) && waitOpened(dialog) && forms.size() == 1,
          "the real coloralpha form did not open");
    const auto opaque = QStringLiteral("opaque");
    const auto transparent = QStringLiteral("transparent");
    const auto invalid = QStringLiteral("invalid");
    const auto selectedColor = [&](const QString &name) {
        auto *pickerButton = control(name);
        return pickerButton ? pickerButton->property("selectedColor").value<QColor>() : QColor{};
    };
    const auto colorIs = [&](const QString &name, QRgb expected) {
        const auto color = selectedColor(name);
        return color.isValid() && color.rgba() == expected;
    };
    CHECK(colorIs(opaque, qRgba(0x12, 0x34, 0x56, 255)) &&
          colorIs(transparent, qRgba(0xab, 0xcd, 0xef, 0)) && colorIs(invalid, qRgba(0, 0, 0, 255)),
          "ASS coloralpha must map 00 to opaque, FF to transparent, and invalid input to opaque black");
    CHECK(set(control(opaque), "selectedColor", QColor(0x12, 0x34, 0x56, 0)) &&
          set(control(transparent), "selectedColor", QColor(0xab, 0xcd, 0xef, 255)) &&
          click(button(0)) && finish(automation, finished, true) && waitClosed(dialog) &&
          clean() && undo->canRedo() && errors.isEmpty(), "coloralpha values did not round-trip to Lua" << errors);
    forms.clear();

    CHECK(run(automation, QStringLiteral("Confirm")) && waitOpened(dialog) && forms.size() == 1 && clean(),
          "confirmation fixture did not open with its tentative edit isolated");
    CHECK(text(button(0)) == QStringLiteral("OK") && text(button(1)) == QStringLiteral("Cancel") && click(button(1)) &&
          finish(automation, finished, false) && waitClosed(dialog) && clean() && undo->canRedo() && errors.isEmpty(),
          "real Cancel button did not return false and roll back the tentative change" << errors);
    forms.clear();
    CHECK(run(automation, QStringLiteral("Confirm")) && waitOpened(dialog) && flag(button(0), "highlighted") && click(button(0)) &&
          finish(automation, finished, true) && waitClosed(dialog) && row(0) == QStringLiteral("Stage16B-Confirmed"),
          "real default OK did not confirm the fixture" << errors);
    forms.clear();
    undo->undo();
    CHECK(clean() && undo->canRedo(), "confirmation should commit in exactly one undo step");

    CHECK(run(automation, QStringLiteral("Progress")) && waitOpened(progress), "production progress dialog did not open");
    auto *progressBar = named(progress, QStringLiteral("automationProgressBar"));
    auto *progressLog = named(progress, QStringLiteral("automationProgressLog"));
    auto *progressCancel = named(progress, QStringLiteral("automationProgressCancelButton"));
    CHECK(progressBar && progressLog && progressCancel && QTest::qWaitFor([&] {
        return progressBar->property("value").toDouble() == 42 && text(progressLog).contains(QStringLiteral("stage16b progress log"));
    }, timeout) && text(progress, "title") == QStringLiteral("Stage 16B progress") &&
          text(named(progress, QStringLiteral("automationProgressTask"))) == QStringLiteral("Waiting for cancellation") &&
          automation->busy() && finished.isEmpty() && clean(), "progress values/log were not rendered or the temporary edit leaked");
    CHECK(captureBoth(window, i18n, artifacts, QStringLiteral("stage16b-progress"), progressCancel, "text", QStringLiteral("Cancel")),
          "progress failed to retranslate or capture in both locales");
    CHECK(click(progressCancel) && finish(automation, finished, false) && clean() && undo->canRedo() && errors.isEmpty(),
          "progress Cancel failed to stop and roll back the worker" << errors);
    if (flag(progress, "opened")) CHECK(click(progressCancel), "cannot dismiss the completed progress log");
    CHECK(waitClosed(progress), "progress dialog remained open");

    CHECK(run(automation, QStringLiteral("File cancel")) && QTest::qWaitFor([&] { return flag(picker, "visible"); }, timeout) &&
          picks.size() == 1 && text(picker, "title") == QStringLiteral("Stage 16B cancel picker") && automation->busy(),
          "real Automation file dialog was not opened for rejection" << errors);
    CHECK(QMetaObject::invokeMethod(picker, "reject") && finish(automation, finished, false) && waitClosed(picker) &&
          clean() && undo->canRedo() && errors.isEmpty(), "file picker reject did not return nil to Lua" << errors);
    picks.clear();
    CHECK(run(automation, QStringLiteral("File")) && QTest::qWaitFor([&] { return flag(picker, "visible"); }, timeout) &&
          picks.size() == 1 && picks.first().value(0).toMap().value(QStringLiteral("multiple")).toBool() &&
          text(picker, "title") == QStringLiteral("Stage 16B pick text") && automation->busy(),
          "real Automation multi-file picker did not open" << errors);
    const auto nameFilters = picker->property("nameFilters").toStringList();
    CHECK(nameFilters == QStringList{QStringLiteral("Text files (*.txt)")}, "Automation file picker lost the Lua wildcard" << nameFilters);
    const auto inputUrl = QUrl::fromLocalFile(input);
    if (!selectAndAccept(picker, inputUrl, true)) {
        qInfo() << "Stage 16B programmatic file-result fallback: Qt picker cannot accept an injected selection; using completeFile";
        automation->completeFile({inputUrl});
        CHECK(QMetaObject::invokeMethod(picker, "reject"), "cannot dismiss the Automation picker after fallback");
    }
    CHECK(finish(automation, finished, true) && waitClosed(picker) && row(0) == QStringLiteral("Stage16B-File"),
          "file picker selection did not return the requested local file to Lua" << errors);
    undo->undo();
    CHECK(clean(), "file-pick macro undo did not restore the original document");

    picks.clear();
    CHECK(run(automation, QStringLiteral("Single open")) && QTest::qWaitFor([&] { return flag(picker, "visible"); }, timeout) &&
          picks.size() == 1 && picks.first().value(0).toMap().value(QStringLiteral("filters")).toStringList() ==
              QStringList({QStringLiteral("Text (*.txt *.md)"), QStringLiteral("All (*)")}),
          "single open or wx wildcard conversion failed");
    CHECK(selectAndAccept(picker, inputUrl) && finish(automation, finished, true) && row(0) == QStringLiteral("Single open ok"),
          "single picker did not return a Lua string" << errors);
    undo->undo();
    CHECK(clean(), "single open changed undo baseline");

    picks.clear();
    const auto saveUrl = QUrl::fromLocalFile(files.filePath(QStringLiteral("保存文件.txt")));
    CHECK(run(automation, QStringLiteral("Save path")) && QTest::qWaitFor([&] { return flag(picker, "visible"); }, timeout) &&
          picks.size() == 1 && picks.first().value(0).toMap().value(QStringLiteral("dontPrompt")).toBool(),
          "Unicode save dialog not opened with dont_prompt");
    if (!selectAndAccept(picker, saveUrl)) {
        qInfo() << "Stage 16B programmatic save-result fallback: offscreen Qt save dialog";
        automation->completeFile({saveUrl});
        CHECK(QMetaObject::invokeMethod(picker, "reject") && waitClosed(picker), "cannot dismiss save picker");
    }
    CHECK(finish(automation, finished, true) && row(0) == QStringLiteral("Save path ok"),
          "save did not return the Unicode Lua path" << errors);
    undo->undo();
    CHECK(clean(), "save changed undo baseline");

    // The shipped lead-in script does not call dialog.display. The above
    // confirmation fixture proves that path independently; here invoke stock code.
    grid->selectSingle(0);
    grid->toggleSelect(1);
    auto *automationMenu = topMenu(window, QStringLiteral("automationMenu"));
    CHECK(openMenu(automationMenu),
          "could not open the real Automation menu to validate lead-in");
    const auto leadIn = entry(automation->menu(), QStringLiteral("Automatic karaoke lead-in"));
    auto *leadItem = menuItem(automationMenu, QStringLiteral("automationMacro_%1").arg(leadIn.value(QStringLiteral("id")).toInt()));
    CHECK(leadIn.value(QStringLiteral("enabled")).toBool() && flag(leadItem, "enabled") &&
          QMetaObject::invokeMethod(automationMenu, "close") && waitClosed(automationMenu) &&
          QMetaObject::invokeMethod(leadItem, "triggered") && finish(automation, finished, true),
          "official Automatic karaoke lead-in did not run from the production menu" << errors);
    const auto led = [&] {
        return row(0) == alpha && row(1) == QStringLiteral("{\\k200}") + beta &&
               row(1, SubtitleGridModel::StartTimeRole) == row(0, SubtitleGridModel::EndTimeRole);
    };
    CHECK(led() && forms.isEmpty() && document->dirty() && undo->undoDescription() == QStringLiteral("Automatic karaoke lead-in"),
          "stock lead-in did not adjust the second timing and add the lead-in tag without a dialog");
    if (flag(progress, "opened")) CHECK(click(progressCancel) && waitClosed(progress), "could not dismiss stock lead-in log");
    undo->undo();
    CHECK(clean() && undo->canRedo(), "official lead-in undo did not restore timing/text");
    undo->redo();
    CHECK(led() && document->dirty() && !undo->canRedo(), "official lead-in redo did not restore timing/text");
    undo->undo();
    CHECK(clean() && undo->canRedo(), "expected clean document and live redo history before export");

    auto *fileMenu = evaluate(object(window->property("menuBar")), QStringLiteral("menuAt(0)"));
    auto *exportMenuItem = menuItem(fileMenu, QStringLiteral("exportSubtitlesMenuItem"));
    const auto openExport = [&] {
        return flag(exportMenuItem, "enabled") && QMetaObject::invokeMethod(exportMenuItem, "triggered") && waitOpened(exportDialog);
    };
    CHECK(openExport(), "production File menu did not open Export Subtitles");
    // Idle language changes reload Lua scripts and give the filters new IDs.
    CHECK(captureBoth(window, i18n, artifacts, QStringLiteral("stage16b-export"), exportDialog, "title", QStringLiteral("Export")) &&
          QMetaObject::invokeMethod(exportDialog, "close") && waitClosed(exportDialog) && openExport(),
          "export dialog failed live retranslation or did not reopen");

    const auto filters = automation->exportFilters();
    const auto configured = entry(filters, QStringLiteral("Stage 16B configured"), QStringLiteral("name"));
    const auto stock = entry(filters, QStringLiteral("Karaoke template"), QStringLiteral("name"));
    const int configuredId = configured.value(QStringLiteral("id")).toInt();
    const int stockId = stock.value(QStringLiteral("id")).toInt();
    CHECK(configuredId != stockId && configured.value(QStringLiteral("hasConfig")).toBool() &&
          stock.value(QStringLiteral("priority")).toInt() == 2000 && !stock.value(QStringLiteral("hasConfig")).toBool() &&
          filters.indexOf(configured) < filters.indexOf(stock), "missing configured filter or stock Karaoke template priority/order" << filters);

    auto exportControl = [&](const QString &name) { return named(exportDialog, name); };
    auto exportFilter = [&](int id) { return exportControl(QStringLiteral("exportFilter_%1").arg(id)); };
    auto *filterList = exportControl(QStringLiteral("exportFilterList"));
    auto *moveUp = exportControl(QStringLiteral("exportMoveUpButton"));
    auto *moveDown = exportControl(QStringLiteral("exportMoveDownButton"));
    auto *configure = exportControl(QStringLiteral("exportConfigureButton"));
    auto *encoding = exportControl(QStringLiteral("exportEncodingCombo"));
    auto *save = exportControl(QStringLiteral("exportSaveButton"));
    auto *target = exportControl(QStringLiteral("exportTargetFileDialog"));
    CHECK(filterList && moveUp && moveDown && configure && encoding && save && target &&
          text(encoding, "currentText") == automation->exportEncoding(), "export controls or encoding default missing");
    auto *clearFilters = exportControl(QStringLiteral("exportSelectNoneButton"));
    CHECK(click(clearFilters) && click(exportFilter(stockId)) && flag(exportFilter(stockId), "checked") && !flag(configure, "enabled") &&
          click(exportFilter(configuredId)) && flag(exportFilter(configuredId), "checked") && flag(configure, "enabled"),
          "could not select the stock and configured filters through their QML checkboxes");
    auto orderIs = [&](int first, int second) {
        return evaluateValue(exportDialog, QStringLiteral("JSON.stringify(orderedIds())")).toString() ==
            QStringLiteral("[%1,%2]").arg(first).arg(second);
    };
    const auto selectedFilterId = [&] {
        auto *selected = object(exportDialog->property("selectedFilter"));
        return selected ? selected->property("filterId").toInt() : 0;
    };
    CHECK(orderIs(configuredId, stockId) && selectedFilterId() == configuredId && click(moveDown) &&
          QTest::qWaitFor([&] { return orderIs(stockId, configuredId) && selectedFilterId() == configuredId; }, timeout) &&
          click(moveUp) && QTest::qWaitFor([&] { return orderIs(configuredId, stockId); }, timeout) &&
          click(moveDown) && QTest::qWaitFor([&] { return orderIs(stockId, configuredId) && selectedFilterId() == configuredId; }, timeout),
          "Move Up/Down did not reorder the selected filters or preserve the current filter");
    CHECK(click(configure) && waitOpened(dialog) && forms.size() == 1 && automation->busy() &&
          text(control(QStringLiteral("prefix"))) == QStringLiteral("Unconfigured-"),
          "Configure did not launch the configured filter's real Lua form" << errors);
    CHECK(set(control(QStringLiteral("prefix")), "text", QStringLiteral("Configured-")) && click(button(0)) &&
          waitClosed(dialog) && finish(automation, finished, true) && waitOpened(exportDialog) && clean(),
          "filter configuration did not complete without modifying the source" << errors);
    forms.clear();
    CHECK(QTest::qWaitFor([&] { return flag(configure, "enabled"); }, timeout) &&
          QMetaObject::invokeMethod(configure, "clicked") && waitOpened(dialog), "could not reopen filter configuration");
    CHECK(text(control(QStringLiteral("prefix"))) == QStringLiteral("Configured-"),
          "filter configuration lost its saved value" << text(control(QStringLiteral("prefix"))));
    CHECK(set(control(QStringLiteral("prefix")), "text", QStringLiteral("Must-not-save-")) && click(button(1)),
          "could not edit and cancel filter configuration");
    CHECK(waitClosed(dialog) && finish(automation, finished, false) && clean(),
          "filter configuration did not cancel cleanly" << errors << finished);
    forms.clear();

    const auto selectedEncoding = QStringLiteral("Western (Windows-1252)");
    const int encodingIndex = automation->exportEncodings().indexOf(selectedEncoding);
    CHECK(encodingIndex >= 0 && set(encoding, "currentIndex", encodingIndex) && text(encoding, "currentText") == selectedEncoding,
          "could not select Windows-1252 in the real encoding combo");
    CHECK(orderIs(stockId, configuredId) && text(encoding, "currentText") == selectedEncoding,
          "export dialog lost checked order/encoding");
    CHECK(flag(save, "enabled") && QMetaObject::invokeMethod(save, "clicked") &&
          QTest::qWaitFor([&] { return flag(target, "visible"); }, timeout) && !automation->busy() &&
          text(target, "title") == i18n->t(QStringLiteral("Export subtitles file")), "Export did not open the real target file dialog");
    const auto outputUrl = QUrl::fromLocalFile(output);
    if (!selectAndAccept(target, outputUrl)) {
        qInfo() << "Stage 16B programmatic export-result fallback: Qt picker cannot accept an injected selection; using exportSubtitles";
        CHECK(QMetaObject::invokeMethod(target, "reject") && waitClosed(target) &&
              QMetaObject::invokeMethod(exportDialog, "close") &&
              automation->exportSubtitles(outputUrl, selectedEncoding, {stockId, configuredId}), "cannot submit export fallback" << errors);
    }
    CHECK(finish(automation, finished, true) && waitClosed(exportDialog) && waitClosed(target) && errors.isEmpty(),
          "filtered export did not finish successfully" << errors);
    QFile exported(output);
    CHECK(exported.open(QIODevice::ReadOnly), exported.errorString());
    const auto bytes = exported.readAll();
    CHECK(bytes.count("Configured-Stage16B-Templated\xE9") == 2 && !bytes.contains("\xC3\xA9") &&
          bytes.contains("[Events]") && clean() && undo->canRedo() && QFileInfo(document->filePath()) == QFileInfo(ass),
          "export missing configured Karaoke output, wrong encoding, or modified the source document" << bytes);
#ifdef Q_OS_WIN
    // Permit normal reads/writes, but deny deletion/rename of the existing target.
    // In-place overwrite would therefore succeed and fail the byte check below.
    {
        const auto lockedPath = files.filePath(QStringLiteral("stage16b-locked.ass"));
        const auto original = QByteArrayLiteral("existing\0destination\r\n");
        CHECK(writeFile(lockedPath, original), "cannot create the locked export destination");
        const auto nativePath = QDir::toNativeSeparators(lockedPath).toStdWString();
        const HANDLE handle = CreateFileW(nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE, "could not lock the export destination" << GetLastError());
        const auto unlock = qScopeGuard([handle] { CloseHandle(handle); });
        CHECK(automation->exportSubtitles(QUrl::fromLocalFile(lockedPath), selectedEncoding, {}) &&
              finish(automation, finished, false) && errors.size() == 1 && !errors.first().value(0).toString().isEmpty(),
              "locked destination must fail the export and report one error" << errors);
        QFile locked(lockedPath);
        CHECK(locked.open(QIODevice::ReadOnly) && locked.readAll() == original && clean() && undo->canRedo() &&
              QFileInfo(document->filePath()) == QFileInfo(ass), "locked export changed the destination or the source");
        auto *errorDialog = named(window, QStringLiteral("automationErrorDialog"));
        CHECK(waitOpened(errorDialog) && QMetaObject::invokeMethod(errorDialog, "close") && waitClosed(errorDialog),
              "the locked export error was not presented or could not be dismissed");
        errors.clear();
    }
#endif

    CHECK(openExport(), "could not reopen Export for script changes");
    CHECK(QMetaObject::invokeMethod(exportControl(QStringLiteral("exportSelectAllButton")), "clicked"),
          "could not select all filters");
    const auto includesId = [&](int id) {
        bool ok = false;
        const int index = evaluateValue(exportDialog, QStringLiteral("orderedIds().indexOf(%1)").arg(id)).toInt(&ok);
        return ok && index >= 0;
    };
    CHECK(includesId(-1) && QTest::qWaitFor([&] { return selectedFilterId() == configuredId; }, timeout) &&
          set(encoding, "currentIndex", encodingIndex), "cannot prepare the open Export dialog for script changes"
          << evaluateValue(exportDialog, QStringLiteral("JSON.stringify(orderedIds())")) << selectedFilterId());
    QSignalSpy exportOpened(exportDialog, SIGNAL(opened()));
    CHECK(exportOpened.isValid(), "missing Export dialog opened signal");
    // The export popup is modal. Invoke the same VM actions as the Automation
    // manager while it is open instead of attempting to click behind it.
    automation->reloadAutoload();
    const auto rescanned = entry(automation->exportFilters(), QStringLiteral("Karaoke template"), QStringLiteral("name"));
    const int rescannedId = rescanned.value(QStringLiteral("id")).toInt();
    const auto noStaleIds = [&] {
        return !includesId(stockId);
    };
    CHECK(!rescanned.isEmpty() && rescannedId != stockId && QTest::qWaitFor([&] {
        return flag(exportDialog, "opened") && !exportFilter(stockId) && exportFilter(rescannedId) &&
               flag(exportFilter(configuredId), "checked") && selectedFilterId() == configuredId && noStaleIds() &&
               text(encoding, "currentText") == selectedEncoding;
    }, timeout) && exportOpened.isEmpty(), "an open Export dialog kept a stale stock filter or lost surviving choices after Rescan");
    CHECK(includesId(-1) && selectedFilterId() == configuredId &&
          flag(configure, "enabled"), "Rescan lost the surviving checked or current filter");
    const auto scripts = automation->scripts();
    const int scriptIndex = scripts.indexOf(entry(scripts, script, QStringLiteral("path")));
    CHECK(scriptIndex >= 0, "could not locate the local script to remove" << scripts);
    automation->removeScript(scriptIndex);
    CHECK(QTest::qWaitFor([&] {
        return flag(exportDialog, "opened") && !exportFilter(configuredId) && selectedFilterId() != configuredId &&
               !flag(configure, "enabled") &&
               evaluateValue(exportDialog, QStringLiteral("JSON.stringify(orderedIds())")).toString() ==
                   QStringLiteral("[-1]") && text(encoding, "currentText") == selectedEncoding;
    }, timeout) && exportOpened.isEmpty(), "an open Export dialog retained the removed filter or lost surviving choices");
    CHECK(QMetaObject::invokeMethod(exportDialog, "close") && waitClosed(exportDialog), "could not dismiss the synced Export dialog");
    automation->addScripts({QUrl::fromLocalFile(script)});

    auto *unsaved = named(window, QStringLiteral("unsavedDialog"));
    CHECK(unsaved, "missing the Unsaved Changes dialog hook");
    grid->selectSingle(0);
    CHECK(run(automation, QStringLiteral("Confirm")) && waitOpened(dialog) && click(button(0)) &&
          finish(automation, finished, true) && waitClosed(dialog) && document->dirty(),
          "could not commit a dirty edit before exercising the close guard" << errors);
    const auto dirtyText = row(0);
    const auto dirtyUndo = undo->undoDescription();
    forms.clear();
    CHECK(run(automation, QStringLiteral("Confirm")) && waitOpened(dialog) && automation->busy() &&
          !flag(unsaved, "visible") && !window->close() && window->isVisible() && !flag(unsaved, "visible"),
          "the close request did not defer while the dirty document's Lua form was active");
    CHECK(finish(automation, finished, false), "busy close did not cancel the task" << finished << automation->busy());
    CHECK(waitClosed(dialog), "cancelled Lua form remained open");
    CHECK(waitOpened(unsaved) && window->isVisible(), "did not reopen the dirty guard"
          << window->property("pendingCloseAfterAutomation") << window->property("pendingAction")
          << unsaved->property("visible") << window->isVisible());
    CHECK(document->dirty() && row(0) == dirtyText && undo->undoDescription() == dirtyUndo && errors.isEmpty(),
          "busy close leaked the cancelled edit" << errors);
    CHECK(click(named(unsaved, QStringLiteral("unsavedCancelButton"))) && waitClosed(unsaved) && window->isVisible() &&
          document->dirty() && row(0) == dirtyText, "Cancel in the unsaved guard did not keep the dirty document open");
    undo->undo();
    CHECK(clean() && undo->canRedo(), "busy close changed the original document or lost undo history");
    forms.clear();
    CHECK(run(automation, QStringLiteral("Confirm")) && waitOpened(dialog) && automation->busy() &&
          !window->close() && window->isVisible(), "the clean window closed before the busy operation unwound");
    CHECK(finish(automation, finished, false) && waitClosed(dialog) && waitClosed(window) && !flag(unsaved, "visible") &&
          clean() && undo->canRedo() && errors.isEmpty(), "clean busy close did not automatically resume and roll back" << errors);
    qInfo() << "Stage 16B integration passed: Lua forms, cancellation, file pickers, lead-in, export and busy close";
    return 0;
}
