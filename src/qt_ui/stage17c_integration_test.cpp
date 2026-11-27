#include "../ass_dialogue.h"
#include "stage17a_attachments_test.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/search_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/theme_vm.h"
#include "viewmodels/translation_service.h"

#include <QImage>
#include <QRegularExpression>
#include <QSignalSpy>

namespace {
using stage17a_attachments_test::object;
using stage17a_attachments_test::role;
using stage17a_attachments_test::timeout;
using stage17a_attachments_test::visualItem;
using stage17a_attachments_test::writeBytes;

#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 17C:" << __LINE__ << message; return 1; \
} } while (false)

constexpr auto fixtureHeader = "\xEF\xBB\xBF" R"ASS([Script Info]
ScriptType: v4.00+
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
)ASS";

QByteArray fixture(bool broken = false) {
    // Actor order moves the leading outside line below the folds without
    // disturbing their endpoints; text order later reverses those endpoints.
    const QByteArray prefix = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,";
    const QByteArray suffix = ",0,0,0,,";
    if (broken) {
        return QByteArray(fixtureHeader) +
            prefix + "A" + suffix + "{=0=1}Malformed marker\n" +
            prefix + "B" + suffix + "{=2}Unmatched opener\n" +
            prefix + "C" + suffix + "{=3}Unmatched closer\n" +
            prefix + "D" + suffix + "Plain line\n" +
            "[Aegisub Extradata]\n"
            "Data: 0,stage17c.keep,ekeep-me\n"
            "Data: 1,_aegi_folddata,enot-a-fold\n"
            "Data: 2,_aegi_folddata,e0;1;5\n"
            "Data: 3,_aegi_folddata,e1;1;6\n";
    }
    return QByteArray(fixtureHeader) +
        prefix + "Z" + suffix + "Before\n" +
        prefix + "B" + suffix + "{=0}Outer start\n" +
        prefix + "C" + suffix + "Outer interior\n" +
        prefix + "D" + suffix + "Inner start\n" +
        prefix + "E" + suffix + "Hidden needle\n" +
        prefix + "F" + suffix + "Inner end\n" +
        prefix + "G" + suffix + "Outer end\n" +
        prefix + "H" + suffix + "After\n" +
        "[Aegisub Extradata]\n"
        "Data: 0,stage17c.keep,ekeep-me\n";
}

QString docText(const SubtitleGridModel &grid, int row) {
    const auto *line = grid.dialogueAt(row);
    return line ? QString::fromStdString(line->Text.get()) : QString{};
}

QVariant rowData(const SubtitleGridModel &grid, int visibleRow, int dataRole) {
    return grid.data(grid.index(visibleRow, 0), dataRole);
}

QByteArray readBytes(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool savedExtraOnLine(const QByteArray &ass, const QString &text) {
    const auto script = QString::fromUtf8(ass);
    const auto entry = QRegularExpression(QStringLiteral(R"((?:^|\n)Data: (\d+),stage17c\.keep,ekeep-me\r?(?=\n|$))"))
        .match(script);
    if (!entry.hasMatch()) return false;
    const auto dialogue = QRegularExpression(QStringLiteral(R"((?:^|\n)Dialogue: [^\r\n]*,\{((?:=\d+)+)\}%1\r?(?=\n|$))")
        .arg(QRegularExpression::escape(text))).match(script);
    return dialogue.hasMatch() && dialogue.captured(1).split(QLatin1Char('=')).contains(entry.captured(1));
}

QQuickItem *gridRow(QQuickItem *root, int docRow) {
    if (root->objectName() == QStringLiteral("subtitleGridRow") && root->isVisible() &&
        root->property("docRow").toInt() == docRow) return root;
    for (auto *child : root->childItems())
        if (auto *row = gridRow(child, docRow)) return row;
    return nullptr;
}

bool clickRow(QQuickItem *row, Qt::MouseButton button = Qt::LeftButton,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    if (!row || !row->window() || !row->isVisible()) return false;
    const auto pos = row->mapToScene(row->boundingRect().center()).toPoint();
    if (!QRect(QPoint(), row->window()->size()).contains(pos)) return false;
    QTest::mouseClick(row->window(), button, modifiers, pos);
    return true;
}

bool captureThemes(QQuickWindow &window, QQuickItem &list, ThemeViewModel &theme) {
    const auto path = qEnvironmentVariable("AEGISUB_STAGE17C_SCREENSHOTS");
    if (path.isEmpty()) return true;
    if (!QDir().mkpath(path)) return false;
    const auto original = theme.darkMode();
    const auto restore = qScopeGuard([&] { theme.setDarkMode(original); });
    const QDir dir(path);
    QImage previous;
    for (bool dark : {false, true}) {
        theme.setDarkMode(dark);
        QTest::qWait(150);
        const auto image = window.grabWindow();
        if (image.isNull()) return false;
        const auto bounds = list.mapRectToScene(list.boundingRect());
        const auto dpr = image.devicePixelRatio();
        const auto crop = QRectF(bounds.topLeft() * dpr, bounds.size() * dpr).toAlignedRect().intersected(image.rect());
        if (crop.isEmpty()) return false;
        const auto grid = image.copy(crop);
        const auto mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
        if (grid == previous || !image.save(dir.filePath(QStringLiteral("line-folds-%1.png").arg(mode))) ||
            !grid.save(dir.filePath(QStringLiteral("line-folds-grid-%1.png").arg(mode)))) return false;
        previous = grid;
    }
    return true;
}
} // namespace

int runStage17CIntegrationTest(QQmlApplicationEngine &engine) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    const auto contextObject = [&](const char *name) {
        return object(engine.rootContext()->contextProperty(QLatin1String(name)));
    };
    auto *theme = qobject_cast<ThemeViewModel *>(contextObject("themeVM"));
    auto *undo = qobject_cast<UndoManager *>(contextObject("undoVM"));
    auto *edit = qobject_cast<EditBoxViewModel *>(contextObject("editBoxVM"));
    auto *search = qobject_cast<SearchViewModel *>(contextObject("searchVM"));
    auto *documentVm = qobject_cast<DocumentViewModel *>(contextObject("documentVM"));
    auto *gridVm = qobject_cast<SubtitleGridModel *>(contextObject("gridModel"));
    CHECK(window && theme && undo && edit && search && documentVm && gridVm,
          "Missing production Main/Theme/Undo/EditBox/Find/Document/Grid objects");
    auto &document = *documentVm;
    auto &grid = *gridVm;
    QTemporaryDir scratch(QStringLiteral("stage17c-fixtures-XXXXXX"));
    CHECK(scratch.isValid(), scratch.errorString());
    const QDir dir(scratch.path());
    const auto source = dir.filePath(QStringLiteral("fold-source.ass"));
    const auto invalid = dir.filePath(QStringLiteral("fold-invalid.ass"));
    const auto saved = dir.filePath(QStringLiteral("fold-roundtrip.ass"));
    const auto cleared = dir.filePath(QStringLiteral("fold-cleared.ass"));
    CHECK(writeBytes(source, fixture()) && writeBytes(invalid, fixture(true)), "Could not write ASS fixtures");
    CHECK(document.openLocalPath(source) && !document.dirty() && !undo->canUndo(), document.lastError());
    window->requestActivate();
    CHECK(QTest::qWaitForWindowActive(window, timeout), "Main did not activate");
    auto *list = visualItem(window->contentItem(), QStringLiteral("subtitleGridList"));
    CHECK(list && QTest::qWaitFor([&] { return list->property("count").toInt() == 8; }, timeout),
          "Could not find the production grid ListView with the fixture loaded");
    const auto focusGrid = [&] {
        list->forceActiveFocus();
        return list->hasActiveFocus();
    };
    const auto key = [&](Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(window, code, modifiers);
        QCoreApplication::processEvents();
    };
    constexpr auto ctrl = Qt::ControlModifier;

    using Grid = SubtitleGridModel;
    const auto mapping = [&](std::initializer_list<int> docs, int total = 8) {
        if (grid.lineCount() != total || grid.rowCount() != int(docs.size()) ||
            grid.visibleRowForDocRow(-1) != -1 || grid.visibleRowForDocRow(total) != -1 ||
            grid.docRowForVisibleRow(-1) != -1 || grid.docRowForVisibleRow(grid.rowCount()) != -1) return false;
        int visible = 0;
        for (int doc = 0; doc < total; ++doc) {
            if (visible < grid.rowCount() && doc == *(docs.begin() + visible)) {
                if (grid.visibleRowForDocRow(doc) != visible || grid.docRowForVisibleRow(visible) != doc ||
                    rowData(grid, visible, Grid::DocRowRole).toInt() != doc ||
                    rowData(grid, visible, Grid::RowNumberRole).toInt() != doc + 1 ||
                    rowData(grid, visible, Grid::TextRole).toString() != docText(grid, doc)) return false;
                ++visible;
            } else if (grid.visibleRowForDocRow(doc) != -1) {
                return false;
            }
        }
        return visible == grid.rowCount() && QTest::qWaitFor([&] {
            return list->property("count").toInt() == grid.rowCount();
        }, timeout);
    };
    const auto allVisible = [&] { return mapping({0, 1, 2, 3, 4, 5, 6, 7}); };
    const auto outerClosed = [&] { return mapping({0, 1, 7}); };
    const auto innerClosed = [&] { return mapping({0, 1, 2, 3, 6, 7}); };
    const auto select = [&](int first, int last) {
        grid.selectSingle(first);
        grid.selectRange(last);
    };
    const auto foldRole = [&](int doc) { return rowData(grid, grid.visibleRowForDocRow(doc), Grid::FoldRole).toInt(); };
    const auto hiddenCount = [&](int doc) {
        return rowData(grid, grid.visibleRowForDocRow(doc), Grid::FoldedHiddenCountRole).toInt();
    };
    for (const auto *name : {"docRow", "isActive", "foldRole", "foldDepth", "foldedHiddenCount"}) {
        CHECK(role(&grid, name) >= 0, "Missing fold-related model role" << name);
    }
    CHECK(allVisible() && !grid.hasFolds() && !grid.selectionHasFolds(), "Unfolded baseline is incorrect");
    grid.selectSingle(1);
    CHECK(!grid.createFoldFromSelection() && !undo->canUndo() && !document.dirty() && allVisible(),
          "A singleton selection created a fold or an undo point");

    select(1, 6);
    CHECK(grid.createFoldFromSelection() && grid.hasFolds() && grid.selectionHasFolds() && outerClosed() &&
          foldRole(1) == Grid::StartCollapsed && hiddenCount(1) == 5 && docText(grid, 4) == QStringLiteral("Hidden needle") &&
          document.dirty() && undo->canUndo(), "Creating a fold did not collapse visible rows only");
    CHECK(grid.activeRow() == 1 && rowData(grid, 1, Grid::ActiveRole).toBool(),
          "Collapsing the active row did not focus the visible opening delimiter");
    CHECK(focusGrid(), "Grid could not take focus for keyboard undo");
    key(Qt::Key_Z, ctrl);
    CHECK(allVisible() && !grid.hasFolds() && !document.dirty() && !undo->canUndo() && undo->canRedo(),
          "Ctrl+Z did not undo creation in one step");
    key(Qt::Key_Y, ctrl);
    CHECK(outerClosed() && grid.hasFolds() && document.dirty(), "Ctrl+Y did not restore the collapsed fold");

    grid.selectSingle(1);
    CHECK(focusGrid(), "Grid could not take focus for Enter");
    key(Qt::Key_Return);
    CHECK(allVisible() && foldRole(1) == Grid::StartOpen && foldRole(2) == Grid::InsideFold &&
          foldRole(6) == Grid::EndFold && grid.activeRow() == 1,
          "Enter in the production grid did not open the selected fold without advancing EditBox");
    key(Qt::Key_Z, ctrl);
    CHECK(outerClosed(), "Ctrl+Z did not reclose the fold opened with Enter");
    key(Qt::Key_Y, ctrl);
    CHECK(allVisible(), "Ctrl+Y did not reopen the fold from Enter");

    const auto beforeRejected = undo->undoDescription();
    select(2, 7);
    CHECK(!grid.createFoldFromSelection() && allVisible() && undo->undoDescription() == beforeRejected,
          "A crossing range created a fold or an undo point");
    select(1, 4);
    CHECK(!grid.createFoldFromSelection() && allVisible() && undo->undoDescription() == beforeRejected,
          "A range sharing an existing boundary created a fold or an undo point");
    select(3, 5);
    CHECK(grid.createFoldFromSelection() && innerClosed() && foldRole(1) == Grid::StartOpen &&
          foldRole(3) == Grid::StartCollapsed && hiddenCount(3) == 2 &&
          rowData(grid, 3, Grid::FoldDepthRole).toInt() > rowData(grid, 1, Grid::FoldDepthRole).toInt(),
          "Nested fold did not retain its own role, depth, and projected count");
    CHECK(captureThemes(*window, *list, *theme), "Dark/light fold screenshots failed");

    grid.selectSingle(3);
    grid.toggleFoldsForSelection();
    CHECK(allVisible() && foldRole(3) == Grid::StartOpen, "Toggle for selection did not open the inner fold");
    grid.toggleFoldAtDocRow(5);
    CHECK(innerClosed() && foldRole(3) == Grid::StartCollapsed, "Toggling an end delimiter failed");
    grid.closeAllFolds();
    CHECK(outerClosed(), "Close All did not collapse the outer fold");
    grid.openAllFolds();
    CHECK(allVisible() && foldRole(1) == Grid::StartOpen && foldRole(3) == Grid::StartOpen,
          "Open All left an inner fold collapsed");
    grid.closeAllFolds();
    CHECK(outerClosed(), "Close All did not collapse both levels");
    grid.toggleFoldAtDocRow(1);
    CHECK(innerClosed(), "Opening only the outer fold lost the inner collapsed state");
    grid.selectSingle(3);
    grid.clearFoldsForSelection();
    CHECK(allVisible() && foldRole(3) == Grid::InsideFold && grid.hasFolds(),
          "Clear Fold(s) removed the outer fold or left the child fold");
    CHECK(focusGrid(), "Grid could not take focus for Clear Fold(s) undo");
    key(Qt::Key_Z, ctrl);
    CHECK(innerClosed(), "Ctrl+Z did not restore the cleared collapsed child fold");
    key(Qt::Key_Y, ctrl);
    CHECK(allVisible() && foldRole(3) == Grid::InsideFold, "Ctrl+Y did not clear only the child fold again");
    key(Qt::Key_Z, ctrl);
    CHECK(innerClosed(), "Could not restore child fold before Clear All");
    grid.clearAllFolds();
    CHECK(allVisible() && !grid.hasFolds() && !grid.selectionHasFolds(), "Clear All left a fold behind");
    key(Qt::Key_Z, ctrl);
    CHECK(innerClosed() && foldRole(1) == Grid::StartOpen, "Clear All was not one undoable commit");
    key(Qt::Key_Y, ctrl);
    CHECK(allVisible() && !grid.hasFolds(), "Ctrl+Y did not reapply Clear All");
    key(Qt::Key_Z, ctrl);
    CHECK(innerClosed(), "Could not restore child fold before ASS round-trip");

    grid.openAllFolds();
    grid.toggleFoldAtDocRow(1);
    CHECK(outerClosed() && document.saveLocalPath(saved) && !document.dirty(), document.lastError());
    const auto bytes = readBytes(saved);
    CHECK(bytes.contains("[Aegisub Extradata]") && bytes.count(",_aegi_folddata,") == 4 &&
          savedExtraOnLine(bytes, QStringLiteral("Outer start")),
          "ASS did not retain four native fold delimiters and unrelated data on the opening line");
    CHECK(document.openLocalPath(saved) && outerClosed() && grid.hasFolds() && !undo->canUndo(),
          "Reopening the ASS did not restore nested fold metadata and a clean baseline");
    grid.toggleFoldAtDocRow(1);
    CHECK(allVisible() && foldRole(3) == Grid::StartOpen, "ASS round-trip did not preserve the child's open state");
    grid.closeAllFolds();
    CHECK(outerClosed(), "Could not hide the nested Find fixture");
    grid.selectSingle(0);
    search->setQuery(QStringLiteral("Hidden needle"));
    QSignalSpy located(search, &SearchViewModel::locateRow);
    CHECK(search->findNext() && allVisible() && grid.activeRow() == 4 && located.size() == 1 &&
          located.first().first().toInt() == 4 && QTest::qWaitFor([&] { return edit->text() == QStringLiteral("Hidden needle"); }, timeout),
          "Find did not expand both ancestor folds, locate the document row, and refresh EditBox");
    edit->setText(QStringLiteral("Edited hidden needle"));
    CHECK(docText(grid, 4) == QStringLiteral("Edited hidden needle") && docText(grid, 1) == QStringLiteral("Outer start"),
          "EditBox edited a different document row after Find revealed the hidden row");

    // Sorting with a valid endpoint order changes the projection's offset;
    // sorting the delimiters in reverse must not leave any rows hidden.
    grid.selectSingle(1);
    grid.toggleFoldAtDocRow(1);
    CHECK(outerClosed(), "Could not reclose the outer fold before sort");
    grid.sortAllLines(Grid::SortActor);
    CHECK(mapping({0, 6, 7}) && docText(grid, 0) == QStringLiteral("Outer start") &&
          docText(grid, 7) == QStringLiteral("Before"), "Stable fold delimiters did not follow ASS order after Sort by Actor");
    grid.sortAllLines(Grid::SortText);
    CHECK(allVisible(), "Reversed fold delimiters still hid document rows after Sort by Text");
    CHECK(focusGrid(), "Grid could not take focus for Sort/Delete undo");
    key(Qt::Key_Z, ctrl);
    CHECK(mapping({0, 6, 7}) && foldRole(0) == Grid::StartCollapsed, "Undo Sort did not restore the collapsed projection");
    grid.selectSingle(0);
    key(Qt::Key_Delete, ctrl);
    CHECK(mapping({0, 1, 2, 3, 4, 5, 6}, 7) && docText(grid, 0) == QStringLiteral("Outer interior") &&
          docText(grid, 2) == QStringLiteral("Edited hidden needle"),
          "Deleting the visible start deleted hidden dialogue or left stale projection entries");
    key(Qt::Key_Z, ctrl);
    CHECK(mapping({0, 6, 7}) && docText(grid, 0) == QStringLiteral("Outer start"),
          "Undo Delete did not restore the original boundary and visibility");

    CHECK(document.openLocalPath(invalid) && mapping({0, 1, 2, 3}, 4) && !document.dirty() && !undo->canUndo(),
          "Malformed or unmatched persisted markers hid rows or dirtied the document on load");
    CHECK(foldRole(0) == Grid::NoFold && foldRole(1) == Grid::NoFold && foldRole(2) == Grid::NoFold,
          "Malformed or unmatched delimiters received visible fold roles");
    grid.clearAllFolds();
    CHECK(mapping({0, 1, 2, 3}, 4) && document.saveLocalPath(cleared), document.lastError());
    const auto clearBytes = readBytes(cleared);
    CHECK(!clearBytes.contains(",_aegi_folddata,") && savedExtraOnLine(clearBytes, QStringLiteral("Malformed marker")),
          "Clearing malformed folds left markers or discarded unrelated extradata");
    qInfo() << "Stage 17C line folds passed";
    return 0;
}

#undef CHECK
