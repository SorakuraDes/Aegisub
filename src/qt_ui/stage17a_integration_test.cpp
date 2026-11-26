#include "../ass_dialogue.h"
#include "viewmodels/subtitle_grid_model.h"
#include "aegisub_core.h"
#include "stage17a_attachments_test.h"
#include "viewmodels/translation_service.h"

#include <QClipboard>
#include <QImage>
#include <QProcess>
#include <cmath>

namespace {
using stage17a_attachments_test::object;
using stage17a_attachments_test::qmlObject;
using stage17a_attachments_test::writeBytes;
constexpr int timeout = 5000;

#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 17A:" << __LINE__ << message; return 1; \
} } while (false)

template<class T>
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

bool visible(QObject *item) { return item && item->property("visible").toBool(); }

double luminance(QColor color) {
    const auto linear = [](double x) { return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

bool captureThemes(QQuickWindow *window, QObject *theme, TranslationService *i18n,
                   const QString &dir, const QString &name, const QStringList &buttons,
                   bool attachments = false) {
    const auto originalTheme = theme->property("darkMode");
    const auto restore = qScopeGuard([&] {
        theme->setProperty("darkMode", originalTheme);
        i18n->setLanguage(QStringLiteral("en_US"));
    });
    for (bool dark : {false, true}) {
        if (!theme->setProperty("darkMode", dark)) return false;
        for (bool chinese : {false, true}) {
            if (!i18n->setLanguage(chinese ? QStringLiteral("zh_CN") : QStringLiteral("en_US"))) return false;
            QTest::qWait(150);
            if (chinese && name == QStringLiteral("select-lines")) {
                auto *check = visualChild(window->contentItem(), QStringLiteral("dialogueCheckBox"));
                if (!check || check->property("text").toString() != QStringLiteral("\u5bf9\u8bdd")) {
                    qWarning() << "Dialogue translation" << (check ? check->property("text") : QVariant{});
                    return false;
                }
            }
            const QImage image = window->grabWindow();
            if (image.isNull()) return false;
            const auto pixels = [&](QQuickItem *item) {
                const auto scene = item->mapRectToScene(item->boundingRect());
                const auto scale = image.devicePixelRatio();
                return QRectF(scene.topLeft() * scale, scene.size() * scale).toAlignedRect().intersected(image.rect());
            };
            for (const auto &id : buttons) {
                auto *button = visualChild(window->contentItem(), id);
                if (!button || !button->isVisible() || !button->isEnabled()) return false;
                auto *label = qobject_cast<QQuickItem *>(object(button->property("contentItem")));
                if (!label || (chinese && !attachments && label->property("text").toString() != QStringLiteral("\u786e\u5b9a"))) return false;
                const auto fill = button->property("fillColor").value<QColor>();
                const auto ink = label->property("color").value<QColor>();
                const double a = luminance(fill), b = luminance(ink);
                if ((qMax(a, b) + .05) / (qMin(a, b) + .05) < 4.5) return false;
                auto *background = qobject_cast<QQuickItem *>(object(button->property("background")));
                if (!background) return false;
                const auto fillRegion = pixels(background).adjusted(7, 4, -7, -4);
                int fillPixels = 0, inkPixels = 0;
                for (int y = fillRegion.top(); y <= fillRegion.bottom(); ++y)
                    for (int x = fillRegion.left(); x <= fillRegion.right(); ++x)
                        fillPixels += image.pixelColor(x, y).rgb() == fill.rgb();
                const auto region = pixels(label);
                for (int y = region.top(); y <= region.bottom(); ++y)
                    for (int x = region.left(); x <= region.right(); ++x) {
                        const auto c = image.pixelColor(x, y);
                        inkPixels += std::abs(c.red() - ink.red()) + std::abs(c.green() - ink.green()) + std::abs(c.blue() - ink.blue()) < 150;
                    }
                if (fillRegion.isEmpty() || fillPixels < fillRegion.width() * fillRegion.height() / 2 || inkPixels < 3) {
                    qWarning() << id << dark << chinese << region << fill << ink << fillPixels << inkPixels;
                    return false;
                }
            }
            if (attachments) {
                auto *background = visualChild(window->contentItem(), QStringLiteral("attachmentsBackground"));
                if (!background) return false;
                const auto color = background->property("color").value<QColor>();
                const auto r = pixels(background).adjusted(12, 12, -12, -12);
                if (color.alpha() != 255 || r.width() < 300 || r.height() < 300) return false;
                for (double fraction : {.5, .7, .8}) {
                    const int y = r.top() + int(r.height() * fraction);
                    for (int x = r.left(); x <= r.right(); ++x)
                        if (image.pixelColor(x, y).rgb() != color.rgb()) return false;
                }
            }
            const auto file = name + (chinese ? "-zh" : "-en") + (dark ? "-dark.png" : "-light.png");
            if (!dir.isEmpty() && !image.save(QDir(dir).filePath(file))) return false;
        }
    }
    return true;
}

QStringList entries(const SubtitleGridModel *grid) {
    QStringList result;
    for (int row = 0; row < grid->lineCount(); ++row)
        result.push_back(QString::fromStdString(grid->dialogueAt(row)->GetEntryData()));
    return result;
}

QString body(const SubtitleGridModel *grid, int row) {
    return QString::fromStdString(grid->dialogueAt(row)->Text.get());
}

constexpr auto fixture = R"ASS([Script Info]
ScriptType: v4.00+
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
Style: Accent,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:01.00,Default,Alice,0,0,0,,{\i1}Alpha
Dialogue: 0,0:00:01.00,0:00:02.00,Accent,Bob,0,0,0,Flash,beta
Comment: 0,0:00:02.00,0:00:03.00,Default,Alice,0,0,0,,alpha
Dialogue: 0,0:00:03.00,0:00:04.00,Default,Carol,0,0,0,Other,gamma
)ASS";
}

int runStage17AIntegrationTest(QQmlApplicationEngine &engine, bool restart) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    auto *grid = vm<SubtitleGridModel>(engine, "gridModel");
    auto *document = vm<DocumentViewModel>(engine, "documentVM");
    auto *undo = vm<UndoManager>(engine, "undoVM");
    auto *i18n = vm<TranslationService>(engine, "i18n");
    auto *theme = vm<QObject>(engine, "themeVM");
    CHECK(window && grid && document && undo && i18n && theme, "missing main window or VMs");
    auto *select = vm<QObject>(engine, "selectLinesVM");
    auto *paste = vm<QObject>(engine, "pasteOverVM");
    CHECK(select && paste, "missing Select Lines / Paste Over VMs");
    const auto persisted = [&] {
        return select->property("query").toString() == QStringLiteral("restart-\u4e2d\u6587") &&
               select->property("condition").toInt() == 1 && select->property("matchMode").toInt() == 2 &&
               select->property("caseSensitive").toBool() && select->property("field").toInt() == 3 &&
               !select->property("includeDialogue").toBool() && select->property("includeComments").toBool() &&
               select->property("selectionAction").toInt() == 3;
    };
    if (restart) {
        CHECK(QMetaObject::invokeMethod(window, "openSelectLines") && persisted(), "Select Lines settings not restored after restart");
        qInfo() << "Stage 17A restart passed";
        return 0;
    }

    QTemporaryDir scratch(QStringLiteral("stage17a-fixtures-XXXXXX"));
    CHECK(scratch.isValid(), scratch.errorString());
    const QString shots = qEnvironmentVariable("AEGISUB_STAGE17A_SCREENSHOTS");
    CHECK(shots.isEmpty() || QDir().mkpath(shots), "cannot create screenshot directory");
    const auto subtitle = QDir(scratch.path()).filePath(QStringLiteral("select-paste.ass"));
    CHECK(writeBytes(subtitle, fixture) && document->openLocalPath(subtitle) && grid->lineCount() == 4,
          "cannot open fixture");
    const auto baseline = entries(grid);
    CHECK(QTest::qWaitForWindowActive(window, timeout), "main window did not activate");
    auto *gridView = visualChild(window->contentItem(), QStringLiteral("subtitleGridList"));
    auto *editor = visualChild(window->contentItem(), QStringLiteral("editBoxTextArea"));
    CHECK(gridView && editor, "cannot locate grid and body editor");
    CHECK(QMetaObject::invokeMethod(window, "openSelectLines"), "cannot open Select Lines");
    auto *selectionWindow = qobject_cast<QQuickWindow *>(object(window->property("selectLinesWindow")));
    CHECK(selectionWindow && selectionWindow->isVisible(), "Select Lines window not visible");
    const auto match = [&](const QString &query, int mode, int field = 0, int condition = 0, int action = 0) {
        bool result = false;
        select->setProperty("query", query);
        select->setProperty("matchMode", mode);
        select->setProperty("field", field);
        select->setProperty("condition", condition);
        select->setProperty("selectionAction", action);
        return QMetaObject::invokeMethod(select, "applySelection", Q_RETURN_ARG(bool, result)) && result;
    };
    const auto selected = [&](std::initializer_list<int> rows) { return grid->sortedSelectedRows() == QList<int>(rows); };
    CHECK(match(QStringLiteral("{\\i1}Alpha"), 0) && selected({0}), "Exact Text did not preserve tags");
    CHECK(match(QStringLiteral("ALPHA"), 1) && selected({0}), "case-folded Contains or default line filter failed");
    select->setProperty("caseSensitive", true);
    CHECK(match(QStringLiteral("alpha"), 1) && selected({}) && grid->activeRow() == 0, "empty set moved active row or ignored case");
    select->setProperty("caseSensitive", false);
    CHECK(match(QStringLiteral("^b.*a$"), 2) && selected({1}) && grid->activeRow() == 1, "valid regexp failed");
    CHECK(!match(QStringLiteral("["), 2) && selected({1}), "invalid regexp succeeded or changed selection");
    CHECK(match(QStringLiteral("Accent"), 0, 1) && selected({1}), "Style field failed");
    CHECK(match(QStringLiteral("Alice"), 0, 2, 0, 1) && selected({0, 1}) && grid->activeRow() == 1, "Actor/Add failed");
    CHECK(match(QStringLiteral("Flash"), 0, 3, 0, 2) && selected({0}) && grid->activeRow() == 0, "Effect/Subtract failed");
    CHECK(match(QStringLiteral("Alice"), 0, 2, 1) && selected({1, 3}), "Doesn't Match should still filter comments");
    CHECK(match(QStringLiteral("beta"), 0, 0, 0, 3) && selected({1}), "Intersect failed");
    select->setProperty("includeDialogue", false);
    CHECK(select->property("includeDialogue").toBool() && !select->property("includeComments").toBool(), "type-filter fallback failed");
    select->setProperty("includeComments", true);
    select->setProperty("includeDialogue", false);
    CHECK(match(QStringLiteral("alpha"), 0) && selected({2}), "Comment-only filter failed");
    CHECK(entries(grid) == baseline && !document->dirty() && !undo->canUndo(), "Select Lines changed document or undo state");
    CHECK(captureThemes(selectionWindow, theme, i18n, shots, QStringLiteral("select-lines"), {QStringLiteral("okButton")}),
          "Select Lines appearance regression");
    select->setProperty("query", QStringLiteral("restart-\u4e2d\u6587"));
    select->setProperty("condition", 1);
    select->setProperty("matchMode", 2);
    select->setProperty("caseSensitive", true);
    select->setProperty("field", 3);
    select->setProperty("selectionAction", 3);
    CHECK(persisted() && selectionWindow->close(), "cannot close configured Select Lines window");
    QString optionError;
    CHECK(aegisub_qt::FlushOptions(&optionError), optionError);
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--stage17a-restart")});
    CHECK(child.waitForFinished(15000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 &&
          child.readAllStandardOutput().contains("Stage 17A restart passed") && child.readAllStandardError().isEmpty(),
          "configuration child restart failed" << child.errorString());
    auto *pasteWindow = qobject_cast<QQuickWindow *>(qmlObject(window, QStringLiteral("pasteOverDialog")));
    CHECK(pasteWindow, "missing Paste Over window");
    const auto invoke = [](QObject *target, const char *method) { return QMetaObject::invokeMethod(target, method); };
    const auto activateGrid = [&] {
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window, timeout)) return false;
        gridView->forceActiveFocus();
        return gridView->hasActiveFocus();
    };
    auto *clipboard = QGuiApplication::clipboard();
    const auto openPaste = [&](const QString &text) {
        clipboard->setText(text);
        return activateGrid() && (QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier), true) &&
               QTest::qWaitFor([&] { return pasteWindow->isVisible(); }, timeout);
    };
    grid->selectSingle(1);
    CHECK(openPaste(QStringLiteral("  first  \r\n\r\nsecond\nthird\nfourth")), "Ctrl+Shift+V did not open grid Paste Over");
    const auto oldMask = paste->property("fields").toList();
    CHECK(oldMask.size() == 11, "legacy field mask was not padded");
    for (int bit = 0; bit < 11; ++bit)
        CHECK(oldMask[bit].toBool() == (bit == 9), "legacy mask did not preserve Effect at bit 9");
    CHECK(invoke(paste, "selectTimes") && pasteWindow->close(), "could not close/cancel Paste Over");
    CHECK(!document->dirty() && openPaste(QStringLiteral("first\r\n\r\nsecond\nthird\nfourth")) &&
          paste->property("fields").toList() == oldMask, "Cancel changed data or saved field mask");
    CHECK(captureThemes(pasteWindow, theme, i18n, shots, QStringLiteral("paste-over"), {QStringLiteral("pasteOverOk")}),
          "Paste Over appearance regression");
    CHECK(invoke(paste, "selectText") && invoke(paste, "accept") && pasteWindow->close() &&
          body(grid, 1) == "first" && body(grid, 2) == "second" && body(grid, 3) == "third" &&
          grid->lineCount() == 4 && selected({1}) && document->dirty(), "single selection, plain text or source overflow failed");
    CHECK(activateGrid(), "main window did not regain focus");
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    CHECK(entries(grid) == baseline && !document->dirty() && !undo->canUndo(), "one Ctrl+Z did not restore batch paste");
    grid->selectSingle(0);
    grid->toggleSelect(2);
    grid->toggleSelect(3);
    CHECK(openPaste(QStringLiteral("Comment: 4,0:00:00.20,0:00:00.90,Accent,Pat,1,2,3,FX,one\n"
                                   "Dialogue: 5,0:00:01.00,0:00:02.50,Default,Eve,4,5,6,EF,two")) &&
          invoke(paste, "selectAll") && invoke(paste, "accept") && pasteWindow->close(), "cannot apply multi-row ASS paste");
    const auto multiple = entries(grid);
    CHECK(multiple[0] == "Comment: 4,0:00:00.20,0:00:00.90,Accent,Pat,1,2,3,FX,one" &&
          multiple[2] == "Dialogue: 5,0:00:01.00,0:00:02.50,Default,Eve,4,5,6,EF,two" &&
          multiple[1] == baseline[1] && multiple[3] == baseline[3] && selected({0, 2, 3}),
          "multi-row paste did not respect field order or source underrun" << multiple);
    CHECK(activateGrid(), "grid did not regain focus for undo");
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    CHECK(entries(grid) == baseline && !document->dirty(), "multi-row paste undo failed");
    editor->forceActiveFocus();
    CHECK(editor->hasActiveFocus(), "EditBox did not take focus");
    clipboard->setText(QStringLiteral("focus probe"));
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    CHECK(!pasteWindow->isVisible(), "Ctrl+Shift+V hijacked the text editor");
    // Any native text-only paste remains a local EditBox edit, not a Paste Over.
    if (document->dirty()) undo->undo();
    CHECK(entries(grid) == baseline && !document->dirty(), "test left a changed subtitle document");
    if (const int result = runStage17AAttachmentsTest(engine, scratch.path())) return result;

    auto *attachments = qmlObject(window, QStringLiteral("attachmentsDialog"));
    CHECK(attachments && QMetaObject::invokeMethod(window, "openAttachments") &&
          QTest::qWaitFor([&] { return attachments->property("opened").toBool(); }, timeout),
          "could not open attachments screenshot dialog");
    CHECK(captureThemes(window, theme, i18n, shots, QStringLiteral("attachments"),
          {QStringLiteral("attachmentsAttachFontsButton"), QStringLiteral("attachmentsAttachGraphicsButton"),
           QStringLiteral("attachmentsCloseButton")}, true), "Attachments appearance regression");
    CHECK(QMetaObject::invokeMethod(attachments, "close"), "cannot close attachments window");
    qInfo() << "Stage 17A tools passed";
    return 0;
}
