#include "viewmodels/subtitle_grid_model.h" // agi::signal before Qt macros
#include "viewmodels/document_vm.h"
#include "viewmodels/log_vm.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/undo_manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QTest>

namespace {
#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 16A integration:" << __LINE__ << message; return 1; \
} } while (false)

QObject *object(const QVariant &value) {
    if (auto *obj = value.value<QObject *>()) return obj;
    return value.value<QJSValue>().toQObject();
}

template<class T = QObject>
T *vm(QQmlApplicationEngine &engine, const char *name) {
    return qobject_cast<T *>(object(engine.rootContext()->contextProperty(QLatin1String(name))));
}

QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *match = visualChild(child, name)) return match;
    return nullptr;
}

QObject *named(QObject *owner, const char *name, const char *id = nullptr) {
    if (!owner) return nullptr;
    const auto key = QString::fromLatin1(name);
    if (auto *child = owner->findChild<QObject *>(key)) return child;
    if (id && qmlContext(owner)) {
        QQmlExpression expression(qmlContext(owner), owner, QString::fromLatin1(id));
        auto *match = object(expression.evaluate());
        if (match && match->objectName() == key) return match;
    }
    if (auto *window = qobject_cast<QQuickWindow *>(owner)) return visualChild(window->contentItem(), key);
    for (const char *property : {"contentItem", "footer"})
        if (auto *match = visualChild(qobject_cast<QQuickItem *>(object(owner->property(property))), key)) return match;
    return nullptr;
}

bool activate(QObject *control, const char *signal) {
    if (!control || !control->property("enabled").toBool() || !QMetaObject::invokeMethod(control, signal)) return false;
    QCoreApplication::processEvents();
    return true;
}

bool opened(QObject *dialog) {
    return dialog && dialog->property("opened").toBool();
}

bool close(QObject *dialog) {
    return dialog && QMetaObject::invokeMethod(dialog, "close") &&
        QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 2000);
}

bool screenshot(QQuickWindow *window, const QString &artifacts, const char *name) {
    if (artifacts.isEmpty()) return true;
    QTest::qWait(100);
    const auto path = QDir(artifacts).filePath(QString::fromLatin1(name));
    if (!window->grabWindow().save(path)) return false;
    qInfo().noquote() << "Stage 16A screenshot:" << path;
    return true;
}

bool openMenu(QObject *menu) {
    QQmlExpression popup(qmlContext(menu), menu, QStringLiteral("popup(450, 25)"));
    popup.evaluate();
    return !popup.hasError() && QTest::qWaitFor([&] { return opened(menu); }, 2000);
}

QObject *menuItem(QObject *menu, const QString &objectName) {
    if (!menu) return nullptr;
    for (int i = 0; i < menu->property("count").toInt(); ++i) {
        QQmlExpression at(qmlContext(menu), menu, QStringLiteral("itemAt(%1)").arg(i));
        auto *item = object(at.evaluate());
        if (!item) continue;
        if (item->objectName() == objectName) return item;
        if (auto *child = menuItem(object(item->property("subMenu")), objectName)) return child;
    }
    return nullptr;
}

QVariantMap macro(const QVariantList &menu, const QString &title) {
    for (const QVariant &value : menu) {
        const auto entry = value.toMap();
        const auto children = entry.value(QStringLiteral("children")).toList();
        if (children.isEmpty() && entry.value(QStringLiteral("title")) == title) return entry;
        if (auto child = macro(children, title); !child.isEmpty()) return child;
    }
    return {};
}

QVariantMap macro(QObject *automation, const QString &title) {
    return macro(automation->property("menu").toList(), title);
}

bool runMacro(QObject *automation, const QVariantMap &entry, bool expected) {
    bool validId = false;
    const int id = entry.value(QStringLiteral("id")).toInt(&validId);
    bool result = !expected;
    return validId && QMetaObject::invokeMethod(automation, "runMacro", Q_RETURN_ARG(bool, result), Q_ARG(int, id)) &&
        result == expected;
}

int scriptRow(QObject *automation, const QString &fileName) {
    const auto scripts = automation->property("scripts").toList();
    for (int i = 0; i < scripts.size(); ++i)
        if (scripts[i].toMap().value(QStringLiteral("fileName")) == fileName) return i;
    return -1;
}

QStringList texts(SubtitleGridModel *grid) {
    QStringList result;
    for (int i = 0; i < grid->rowCount(); ++i)
        result.append(grid->data(grid->index(i, 0), SubtitleGridModel::TextRole).toString());
    return result;
}

bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size() && file.flush();
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
Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\b1}{\i1}Alpha
Dialogue: 0,0:00:02.00,0:00:04.00,Default,,0,0,0,,Beta
Dialogue: 0,0:00:05.00,0:00:07.00,Default,,0,0,0,,Gamma
Dialogue: 0,0:00:06.00,0:00:08.00,Default,,0,0,0,,Delta
)ass";

constexpr auto luaFixture = R"lua(script_name = "Stage 16A transaction fixture"
script_description = "Tests the macro transaction boundary"
script_author = "Stage 16A"
script_version = "1"

local function change(subs, sel)
    assert(#sel == 1)
    local line = subs[sel[1]]
    line.text = "must not survive"
    subs[sel[1]] = line
    aegisub.set_undo_point("must not survive")
end

aegisub.register_macro("Stage 16A/Nested/Fail", "Forces a Lua stack trace", function(subs, sel)
    local function fail()
        error("stage16a deliberate failure")
    end
    change(subs, sel)
    fail()
end)

aegisub.register_macro("Stage 16A/Nested/Cancel", "Cancels after a change", function(subs, sel)
    change(subs, sel)
    aegisub.cancel()
end)

aegisub.register_macro("Stage 16A/Nested/No-op", "Reassigns unchanged data", function(subs, sel)
    subs[sel[1]] = subs[sel[1]]
    aegisub.set_undo_point("no real change")
end)

local validations = 0
aegisub.register_macro("Stage 16A/Validation probe", "0", function() end, function(_, sel)
    validations = validations + 1
    return #sel == 2, tostring(validations)
end)

aegisub.register_macro("Stage 16A/Edit style", "Update the selected style form", function(subs)
    for i = 1, #subs do
        local line = subs[i]
        if line.class == "style" and line.name == "Default" then
            line.fontname = "Courier New"
            line.fontsize = 37
            subs[i] = line
            aegisub.set_undo_point("Stage 16A style edit")
            return
        end
    end
    error("Default style not found")
end)
)lua";
}

int runStage16AIntegrationTest(QQmlApplicationEngine &engine) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    auto *automation = vm(engine, "automationVM");
    auto *document = vm<DocumentViewModel>(engine, "documentVM");
    auto *grid = vm<SubtitleGridModel>(engine, "gridModel");
    auto *undo = vm<UndoManager>(engine, "undoVM");
    auto *i18n = vm<TranslationService>(engine, "i18n");
    auto *log = vm<LogViewModel>(engine, "logVM");
    CHECK(window && window->isVisible() && automation && document && grid && undo && i18n && log, "missing production window or VM");
    const auto english = QStringLiteral("en_US");
    CHECK(i18n->setLanguage(english), i18n->error());

    auto *bar = object(window->property("menuBar"));
    if (!bar) bar = named(window, "mainMenuBar", "mainMenuBar");
    CHECK(bar, "missing mainMenuBar");
    QObject *menu = nullptr;
    QStringList topMenus;
    for (int i = 0; i < bar->property("count").toInt(); ++i) {
        QQmlExpression at(qmlContext(bar), bar, QStringLiteral("menuAt(%1)").arg(i));
        if (auto *candidate = object(at.evaluate())) {
            topMenus << candidate->objectName() + QStringLiteral(": ") + candidate->property("title").toString();
            if (candidate->objectName() == QStringLiteral("automationMenu")) menu = candidate;
        }
    }
    if (!menu) menu = named(window, "automationMenu", "automationMenu");
    CHECK(menu, "missing automationMenu; menus found:" << topMenus);
    auto *managerAction = menuItem(menu, QStringLiteral("automationManagerMenuItem"));
    auto *rescanAction = menuItem(menu, QStringLiteral("automationReloadAutoloadMenuItem"));
    auto *manager = named(window, "automationManagerDialog", "automationManagerDialog");
    auto *errorDialog = named(window, "automationErrorDialog", "automationErrorDialog");
    CHECK(managerAction, "missing automationManagerMenuItem; menu count:" << menu->property("count"));
    CHECK(rescanAction, "missing automationReloadAutoloadMenuItem; menu count:" << menu->property("count"));
    CHECK(manager, "missing automationManagerDialog");
    CHECK(errorDialog, "missing automationErrorDialog");
    CHECK(menu->property("title").toString() == QStringLiteral("Automation"), "wrong English Automation menu title");
    QSignalSpy errors(automation, SIGNAL(executionError(QString)));
    QSignalSpy scriptsChanged(automation, SIGNAL(scriptsChanged()));
    QSignalSpy menuChanged(automation, SIGNAL(menuChanged()));
    CHECK(errors.isValid() && scriptsChanged.isValid() && menuChanged.isValid(), "missing Automation signals");

    constexpr struct { const char *file; const char *title; } official[] = {
        {"cleantags-autoload.lua", "Clean Tags"},
        {"kara-templater.lua", "Apply karaoke template"},
        {"karaoke-auto-leadin.lua", "Automatic karaoke lead-in"},
        {"macro-1-edgeblur.lua", "Add edgeblur"},
        {"macro-2-mkfullwitdh.lua", "Make fullwidth"},
        {"select-overlaps.moon", "Select overlaps"},
        {"strip-tags.lua", "Strip tags"},
    };
    const auto scripts = automation->property("scripts").toList();
    QSet<int> officialIds;
    for (const auto &expected : official) {
        const int row = scriptRow(automation, QLatin1String(expected.file));
        CHECK(row >= 0, "autoload script not listed" << expected.file << scripts);
        const auto script = scripts[row].toMap();
        CHECK(script.value(QStringLiteral("autoload")).toBool() && script.value(QStringLiteral("error")).toString().isEmpty() &&
              !script.value(QStringLiteral("name")).toString().isEmpty() && !script.value(QStringLiteral("description")).toString().isEmpty() &&
              !script.value(QStringLiteral("author")).toString().isEmpty() && !script.value(QStringLiteral("version")).toString().isEmpty(),
              "official script failed to load or lacks metadata" << script);
        const auto leaf = macro(automation, QLatin1String(expected.title));
        bool idOk = false;
        const int id = leaf.value(QStringLiteral("id")).toInt(&idOk);
        CHECK(idOk && !officialIds.contains(id) && !leaf.value(QStringLiteral("description")).toString().isEmpty() &&
              leaf.contains(QStringLiteral("enabled")) && leaf.contains(QStringLiteral("checked")), "missing or duplicate official macro" << expected.title << leaf);
        officialIds.insert(id);
        qInfo().noquote() << "Stage 16A official registration:" << expected.file << "->" << expected.title;
    }

    const auto artifacts = qEnvironmentVariable("AEGISUB_QT_STAGE16A_ARTIFACTS");
    CHECK(artifacts.isEmpty() || QFileInfo(artifacts).isDir(), "artifact directory does not exist" << artifacts);
    QTemporaryDir temp(QDir(artifacts.isEmpty() ? QDir::tempPath() : artifacts).filePath(QStringLiteral("stage16a-fixture-XXXXXX")));
    CHECK(temp.isValid(), temp.errorString());
    const QString subdir = QStringLiteral("\u81ea\u52a8\u5316 \u00e9");
    const QString scriptName = QStringLiteral("\u6d4b\u8bd5 \u0416.lua");
    const QDir files(QDir(temp.path()).filePath(subdir));
    CHECK(QDir(temp.path()).mkdir(subdir), "cannot create Unicode fixture directory");
    const QString assPath = files.filePath(QStringLiteral("\u5b57\u5e55.ass"));
    const QString scriptPath = files.filePath(scriptName);
    const auto brokenName = QStringLiteral("broken-syntax.lua");
    const auto brokenPath = files.filePath(brokenName);
    CHECK(writeFile(assPath, assFixture) && writeFile(scriptPath, luaFixture) &&
          writeFile(brokenPath, "aegisub.register_macro('Stage 16A/Syntax failure', '', function() end)\nlocal =\n"),
          "cannot write Unicode-path fixtures");
    CHECK(document->openLocalPath(assPath) && grid->rowCount() == 4, document->lastError());
    const auto baseline = texts(grid);
    CHECK(baseline.front() == QStringLiteral("{\\b1}{\\i1}Alpha") && !document->dirty() && !undo->canUndo() && !undo->canRedo(),
          "unexpected subtitle fixture baseline" << baseline);

    const auto leadIn = QStringLiteral("Automatic karaoke lead-in");
    grid->selectSingle(0);
    CHECK(openMenu(menu) && !macro(automation, leadIn).value(QStringLiteral("enabled")).toBool() && close(menu),
          "lead-in should be disabled for one selected row");
    grid->toggleSelect(2);
    const auto selected = grid->sortedSelectedRows();
    const int active = grid->activeRow();
    CHECK(openMenu(menu) && macro(automation, leadIn).value(QStringLiteral("enabled")).toBool(),
          "lead-in should be enabled for two selected rows");
    CHECK(screenshot(window, artifacts, "stage16a-automation-menu-en.png") && close(menu),
          "could not capture or close the open Automation menu");
    const auto edgeblur = macro(automation, QStringLiteral("Add edgeblur"));
    auto *edgeItem = menuItem(menu, QStringLiteral("automationMacro_%1").arg(edgeblur.value(QStringLiteral("id")).toInt()));
    CHECK(edgeItem && edgeItem->property("text").toString() == QStringLiteral("Add edgeblur") && activate(edgeItem, "triggered"),
          "Add edgeblur is not actionable in the production menu");
    auto blurred = baseline;
    blurred[0].prepend(QStringLiteral("{\\be1}"));
    blurred[2].prepend(QStringLiteral("{\\be1}"));
    CHECK(QTest::qWaitFor([&] { return texts(grid) == blurred; }, 2000) &&
          grid->sortedSelectedRows() == selected && grid->activeRow() == active &&
          document->dirty() && undo->canUndo() && !undo->canRedo() && undo->undoDescription() == QStringLiteral("Add edgeblur"),
          "edgeblur should affect only selected rows in one named undo transaction" << texts(grid));
    undo->undo();
    CHECK(texts(grid) == baseline && !document->dirty() && !undo->canUndo() && undo->canRedo(), "edgeblur undo did not restore the clean baseline in one step");
    undo->redo();
    CHECK(texts(grid) == blurred && document->dirty() && undo->canUndo() && !undo->canRedo(), "edgeblur redo did not restore the macro result");

    CHECK(document->openLocalPath(assPath), document->lastError());
    grid->selectSingle(2); // Clean Tags processes the whole document, not just the selection.
    auto cleaned = baseline;
    cleaned[0] = QStringLiteral("{\\b1\\i1}Alpha");
    CHECK(runMacro(automation, macro(automation, QStringLiteral("Clean Tags")), true) && texts(grid) == cleaned &&
          document->dirty() && undo->canUndo(), "Clean Tags did not merge adjacent override blocks" << texts(grid));
    grid->selectSingle(0);
    auto stripped = baseline;
    stripped[0] = QStringLiteral("Alpha");
    CHECK(runMacro(automation, macro(automation, QStringLiteral("Strip tags")), true) && texts(grid) == stripped,
          "Strip tags did not remove overrides" << texts(grid));
    undo->undo();
    CHECK(texts(grid) == cleaned && document->dirty(), "Strip tags did not have its own undo step");

    CHECK(document->openLocalPath(assPath), document->lastError());
    grid->selectSingle(0);
    const QList<int> overlapRows{1, 3};
    CHECK(runMacro(automation, macro(automation, QStringLiteral("Select overlaps")), true) &&
          grid->sortedSelectedRows() == overlapRows && overlapRows.contains(grid->activeRow()) && texts(grid) == baseline &&
          !document->dirty() && !undo->canUndo() && !undo->canRedo(), "MoonScript overlap result did not update only the selection" << grid->sortedSelectedRows());

    scriptsChanged.clear();
    menuChanged.clear();
    const QList<QUrl> urls{QUrl::fromLocalFile(brokenPath), QUrl::fromLocalFile(scriptPath)};
    CHECK(QMetaObject::invokeMethod(automation, "addScripts", Q_ARG(QList<QUrl>, urls)) &&
          scriptsChanged.count() > 0 && menuChanged.count() > 0, "adding local scripts did not notify models");
    const int brokenRow = scriptRow(automation, brokenName);
    CHECK(brokenRow >= 0, "syntax-failed script was not listed");
    const auto broken = automation->property("scripts").toList()[brokenRow].toMap();
    CHECK(!broken.value(QStringLiteral("error")).toString().isEmpty() && !broken.value(QStringLiteral("autoload")).toBool() &&
          macro(automation, QStringLiteral("Syntax failure")).isEmpty() && !macro(automation, QStringLiteral("Fail")).isEmpty(),
          "syntax-failed script published a macro or prevented the healthy script from loading" << broken);
    CHECK(QMetaObject::invokeMethod(automation, "removeScript", Q_ARG(int, brokenRow)) && scriptRow(automation, brokenName) < 0,
          "syntax-failed local script could not be removed");
    int row = scriptRow(automation, scriptName);
    CHECK(row >= 0, "Unicode Lua filename not listed" << automation->property("scripts"));
    const auto local = automation->property("scripts").toList()[row].toMap();
    CHECK(QFileInfo(local.value(QStringLiteral("path")).toString()) == QFileInfo(scriptPath) &&
          !local.value(QStringLiteral("autoload")).toBool() && local.value(QStringLiteral("error")).toString().isEmpty(),
          "Unicode Lua script path or origin incorrect" << local);
    QString info;
    CHECK(QMetaObject::invokeMethod(automation, "scriptInfo", Q_RETURN_ARG(QString, info), Q_ARG(int, row)) &&
          info.contains(scriptName) && info.contains(QStringLiteral("Stage 16A transaction fixture")), "scriptInfo missing Unicode filename or metadata" << info);

    // The validator changes its description on every invocation, so a
    // redundant call is observable even if enabled and checked did not change.
    const auto probe = [&] { return macro(automation, QStringLiteral("Validation probe")); };
    const auto validations = [&] { return probe().value(QStringLiteral("description")).toInt(); };
    grid->selectSingle(0);
    CHECK(openMenu(menu) && validations() > 0 && !probe().value(QStringLiteral("enabled")).toBool() && close(menu),
          "first menu show did not evaluate the Lua validation probe");
    const int initialValidations = validations();
    grid->toggleSelect(2);
    QTest::qWait(150);
    CHECK(!opened(menu) && validations() == initialValidations && !probe().value(QStringLiteral("enabled")).toBool(),
          "Lua validation ran in the background while the Automation menu was closed" << validations());
    CHECK(openMenu(menu) && validations() == initialValidations + 1 && probe().value(QStringLiteral("enabled")).toBool() && close(menu),
          "menu did not revalidate exactly once after the selection changed" << validations());
    CHECK(openMenu(menu) && validations() == initialValidations + 1 && close(menu),
          "unchanged menu reopen re-ran the cached Lua validator" << validations());
    const auto selectionBeforeEdit = grid->sortedSelectedRows();
    const int activeBeforeEdit = grid->activeRow();
    auto *editor = vm(engine, "editBoxVM");
    CHECK(editor && editor->setProperty("text", QStringLiteral("closed menu edit")) && texts(grid) != baseline &&
          grid->sortedSelectedRows() == selectionBeforeEdit && grid->activeRow() == activeBeforeEdit,
          "could not edit text with the menu closed and selection unchanged");
    QTest::qWait(150);
    CHECK(!opened(menu) && validations() == initialValidations + 1,
          "subtitle edits re-ran Lua validation in the background" << validations());
    CHECK(openMenu(menu) && validations() == initialValidations + 2 && close(menu),
          "menu did not revalidate after a content-only commit" << validations());

    CHECK(document->openLocalPath(assPath), document->lastError());
    grid->selectSingle(0);
    CHECK(runMacro(automation, macro(automation, QStringLiteral("Add edgeblur")), true), "cannot seed undo history for rollback checks");
    undo->undo(); // A failed or empty commit must not destroy the existing redo entry.
    grid->selectSingle(1);
    const auto redoName = undo->redoDescription();
    const auto unchanged = [&] {
        return texts(grid) == baseline && grid->activeRow() == 1 && grid->selectionCount() == 1 &&
            !document->dirty() && !undo->canUndo() && undo->canRedo() && undo->redoDescription() == redoName;
    };
    CHECK(unchanged() && errors.isEmpty(), "bad rollback baseline or unexpected error in official scripts" << errors);

    const auto fail = macro(automation, QStringLiteral("Fail"));
    CHECK(menuItem(menu, QStringLiteral("automationMacro_%1").arg(fail.value(QStringLiteral("id")).toInt())),
          "nested Lua macro was not rendered through the recursive menu");
    CHECK(runMacro(automation, fail, false) && errors.count() == 1 && unchanged(), "Lua error must roll back without touching selection or history");
    const auto trace = errors.first().value(0).toString();
    CHECK(trace.contains(QStringLiteral("stage16a deliberate failure")) &&
          trace.contains(QStringLiteral("<anonymous function at lines")) &&
          trace.contains(QStringLiteral("<C function>")),
          "Lua error did not preserve its stack trace" << trace);
    CHECK(QTest::qWaitFor([&] {
        log->refresh(); // The log emitter receives messages on a background thread.
        return log->text().contains(trace);
    }, 2000), "Lua error and traceback did not reach LogViewModel" << trace);
    CHECK(QTest::qWaitFor([&] { return opened(errorDialog); }, 2000) && close(errorDialog), "Lua error was not presented in the production dialog");
    CHECK(runMacro(automation, macro(automation, QStringLiteral("Cancel")), false) && errors.count() == 1 && unchanged() && !opened(errorDialog),
          "cancellation was not silent or did not roll back");
    CHECK(runMacro(automation, macro(automation, QStringLiteral("No-op")), true) && errors.count() == 1 && unchanged(),
          "no-op should not mark the document dirty or destroy redo history");
    undo->redo();
    auto redone = baseline;
    redone[0].prepend(QStringLiteral("{\\be1}"));
    CHECK(texts(grid) == redone && document->dirty(), "rollback checks altered the redo snapshot");

    CHECK(document->openLocalPath(assPath) && QMetaObject::invokeMethod(window, "openStyles"),
          "cannot open the production Styles Manager on the clean fixture");
    auto *styles = vm(engine, "stylesVM");
    auto *styleWindow = qobject_cast<QQuickWindow *>(object(window->property("stylesWindow")));
    CHECK(styles && styleWindow && styleWindow->objectName() == QStringLiteral("automationStylesWindow") &&
          QTest::qWaitFor([&] { return styleWindow->isVisible(); }, 2000), "Styles Manager did not open");
    auto *font = named(styleWindow, "stylesFontField", "fontField");
    auto *size = named(styleWindow, "stylesFontSizeSpin", "fontSizeSpin");
    const auto originalFont = QStringLiteral("Arial");
    const auto updatedFont = QStringLiteral("Courier New");
    const int styleRow = styles->property("currentIndex").toInt();
    const auto styleName = QStringLiteral("Default");
    const auto formShows = [&](const QString &expectedFont, int expectedSize) {
        return styleWindow->isVisible() && styles->property("currentIndex").toInt() == styleRow &&
            styles->property("name").toString() == styleName && font->property("text").toString() == expectedFont &&
            size->property("value").toInt() == expectedSize;
    };
    CHECK(font && size && styleRow >= 0 && formShows(originalFont, 20), "font controls did not show the original Default style");
    CHECK(runMacro(automation, macro(automation, QStringLiteral("Edit style")), true) &&
          QTest::qWaitFor([&] { return formShows(updatedFont, 37); }, 2000) && texts(grid) == baseline &&
          document->dirty() && undo->undoDescription() == QStringLiteral("Stage 16A style edit"),
          "external style commit did not refresh the open form without changing selection");
    undo->undo();
    CHECK(QTest::qWaitFor([&] { return formShows(originalFont, 20); }, 2000) && !document->dirty() && !undo->canUndo(),
          "style undo did not restore the open form and clean state in one step");
    undo->redo();
    CHECK(QTest::qWaitFor([&] { return formShows(updatedFont, 37); }, 2000) && document->dirty(),
          "style redo did not update the open form");
    CHECK(styleWindow->close(), "cannot close the Styles Manager");

    CHECK(activate(managerAction, "triggered") && QTest::qWaitFor([&] { return opened(manager); }, 2000), "Automation Manager did not open");
    auto *scriptList = named(manager, "automationScriptList", "scriptList");
    auto *add = named(manager, "automationScriptAddButton");
    auto *remove = named(manager, "automationScriptRemoveButton");
    auto *reload = named(manager, "automationScriptReloadButton");
    auto *showInfo = named(manager, "automationScriptInfoButton");
    auto *scriptInfoDialog = named(manager, "automationScriptInfoDialog", "infoDialog");
    CHECK(scriptList, "missing automationScriptList");
    CHECK(add && remove && reload && showInfo && scriptInfoDialog,
          "missing manager control(s): Add/Remove/Reload/Info/InfoDialog present=" << bool(add) << bool(remove) << bool(reload) << bool(showInfo) << bool(scriptInfoDialog));
    CHECK(scriptList->property("count").toInt() == automation->property("scripts").toList().size() &&
          manager->property("title").toString() == QStringLiteral("Automation Manager"), "wrong manager title or script count");
    const auto addEnglish = QStringLiteral("Add");
    CHECK(add->property("text").toString() == addEnglish && named(manager, "automationScriptFileDialog", "scriptPicker"), "wrong Add label or missing automationScriptFileDialog");
    CHECK(screenshot(window, artifacts, "stage16a-automation-manager-en.png"), "could not capture the English Automation Manager");
    const auto zh = QStringLiteral("zh_CN");
    const auto translatedEdge = QStringLiteral("Add edgeblur - \u6dfb\u52a0\u8fb9\u89d2\u6a21\u7cca");
    CHECK(i18n->setLanguage(zh) && QTest::qWaitFor([&] {
        const auto leaf = macro(automation, translatedEdge);
        auto *item = leaf.isEmpty() ? nullptr : menuItem(menu, QStringLiteral("automationMacro_%1").arg(leaf.value(QStringLiteral("id")).toInt()));
        return opened(manager) && menu->property("title").toString() == QStringLiteral("\u81ea\u52a8\u5316") &&
            manager->property("title").toString() == QStringLiteral("\u81ea\u52a8\u5316\u811a\u672c\u7ba1\u7406\u5668") &&
            add->property("text").toString() == QStringLiteral("\u8f7d\u5165") && item && item->property("text").toString() == translatedEdge;
    }, 2000), "open manager, menu, or official Lua macro did not retranslate live");
    CHECK(screenshot(window, artifacts, "stage16a-automation-manager-zh.png"), "could not capture the Chinese Automation Manager");
    CHECK(i18n->setLanguage(english) && QTest::qWaitFor([&] {
        return opened(manager) && manager->property("title").toString() == QStringLiteral("Automation Manager") &&
            add->property("text").toString() == addEnglish && !macro(automation, QStringLiteral("Add edgeblur")).isEmpty();
    }, 2000), "manager and official Lua macro did not return to English");

    const auto selectLocal = [&] {
        row = scriptRow(automation, scriptName);
        return row >= 0 && scriptList->setProperty("currentIndex", row) && scriptList->property("currentIndex").toInt() == row;
    };
    const int autoloadRow = scriptRow(automation, QStringLiteral("macro-1-edgeblur.lua"));
    CHECK(autoloadRow >= 0 && scriptList->setProperty("currentIndex", autoloadRow) && !remove->property("enabled").toBool(),
          "manager allows removal of an autoload script");
    auto *infoText = named(scriptInfoDialog, "automationScriptInfoText", "infoText");
    CHECK(infoText, "missing automationScriptInfoText");
    CHECK(selectLocal() && activate(showInfo, "clicked") && QTest::qWaitFor([&] { return opened(scriptInfoDialog); }, 2000) &&
          infoText->property("text").toString().contains(scriptName) && close(scriptInfoDialog),
          "manager could not show information for the Unicode-path script");
    CHECK(writeFile(scriptPath, "script_name = 'Reloaded fixture'\n"
                   "aegisub.register_macro('Stage 16A/Reloaded', 'Reload check', function() end)\n") &&
          selectLocal() && activate(reload, "clicked") && QTest::qWaitFor([&] {
        return !macro(automation, QStringLiteral("Reloaded")).isEmpty() && macro(automation, QStringLiteral("Fail")).isEmpty();
    }, 2000), "manager reload did not replace the script's registered macros");
    CHECK(selectLocal() && activate(remove, "clicked") && QTest::qWaitFor([&] {
        return scriptRow(automation, scriptName) < 0 && macro(automation, QStringLiteral("Reloaded")).isEmpty();
    }, 2000) && close(manager), "manager remove did not remove the script and macros");

    scriptsChanged.clear();
    CHECK(activate(rescanAction, "triggered") && QTest::qWaitFor([&] { return !scriptsChanged.isEmpty(); }, 2000),
          "Automation menu autoload rescan did not update the script list");
    for (const auto &expected : official)
        CHECK(scriptRow(automation, QLatin1String(expected.file)) >= 0 && !macro(automation, QLatin1String(expected.title)).isEmpty(),
              "autoload rescan lost an official registration" << expected.file);
    CHECK(scriptRow(automation, scriptName) < 0 && errors.count() == 1 && !opened(manager) && !opened(errorDialog),
          "autoload rescan restored a manual script, emitted an error, or left a dialog open");
    qInfo().noquote() << "Stage 16A integration passed:" << officialIds.size()
                      << "official macros; execution, undo/redo, rollback, Unicode paths, manager and live i18n verified";
    return 0;
}
