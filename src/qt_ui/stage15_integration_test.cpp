#include "viewmodels/subtitle_grid_model.h" // Parse agi::signal before Qt macros.
#include "viewmodels/document_vm.h"
#include "viewmodels/search_vm.h"
#include "viewmodels/undo_manager.h"
#include "viewmodels/video_vm.h"
#include "ass_dialogue.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace {

constexpr char firstEntry[] = "Dialogue: 2,0:00:00.40,0:00:02.80,Default,Alice,3,4,5,fx,needle first, entry";
constexpr char secondEntry[] = "Dialogue: 0,0:00:00.60,0:00:03.00,Default,Bob,0,0,0,,needle second";
constexpr char thirdEntry[] = "Comment: 1,0:00:01.00,0:00:04.00,Default,Carol,6,7,8,,third";

#define STAGE15_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            qCritical() << "Stage 15 integration:" << message; \
            return 1; \
        } \
    } while (false)

template <typename T>
T *contextObject(QQmlContext *context, const char *name) {
    return qobject_cast<T *>(context->contextProperty(QLatin1String(name)).value<QObject *>());
}

QQuickItem *findGridView(QQuickItem *item, SubtitleGridModel *model) {
    if (item->property("model").value<QObject *>() == model)
        return item;
    for (auto *child : item->childItems()) {
        if (auto *grid = findGridView(child, model))
            return grid;
    }
    return nullptr;
}

QStringList entries(const SubtitleGridModel *grid) {
    QStringList result;
    for (int i = 0; i < grid->rowCount(); ++i)
        result.append(QString::fromStdString(grid->dialogueAt(i)->GetEntryData()));
    return result;
}

QWindow *preferencesWindow(QQuickWindow *mainWindow) {
    const auto value = mainWindow->property("preferencesWindow");
    auto *object = value.value<QObject *>();
    if (!object && value.canConvert<QJSValue>())
        object = value.value<QJSValue>().toQObject();
    return qobject_cast<QWindow *>(object);
}

} // namespace

int runStage15IntegrationTest(QQmlApplicationEngine &engine) {
    const auto roots = engine.rootObjects();
    auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    STAGE15_CHECK(window && window->isVisible(), "expected the visible production Main.qml window");
    auto *context = engine.rootContext();
    auto *grid = contextObject<SubtitleGridModel>(context, "gridModel");
    auto *document = contextObject<DocumentViewModel>(context, "documentVM");
    auto *undo = contextObject<UndoManager>(context, "undoVM");
    auto *search = contextObject<SearchViewModel>(context, "searchVM");
    auto *video = contextObject<VideoViewModel>(context, "videoVM");
    auto *clipboard = QGuiApplication::clipboard();
    STAGE15_CHECK(grid && document && undo && search && video && clipboard,
                  "missing production view model or clipboard");

    QTemporaryDir temporary(QStringLiteral("stage15-fixture-XXXXXX"));
    STAGE15_CHECK(temporary.isValid(), temporary.errorString());
    QFile fixture(temporary.filePath(QStringLiteral("stage15.ass")));
    QByteArray ass = "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n"
                     "\n[V4+ Styles]\n"
                     "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                     "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n"
                     "\n[Events]\n"
                     "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const auto *entry : {firstEntry, secondEntry, thirdEntry}) {
        ass.append(entry);
        ass.append('\n');
    }
    STAGE15_CHECK(fixture.open(QIODevice::WriteOnly) && fixture.write(ass) == ass.size()
                      && fixture.flush(), fixture.errorString());
    fixture.close();
    STAGE15_CHECK(document->openLocalPath(fixture.fileName()), document->lastError());
    const auto first = QString::fromLatin1(firstEntry);
    const auto second = QString::fromLatin1(secondEntry);
    const auto third = QString::fromLatin1(thirdEntry);
    const QStringList baseline{first, second, third};
    STAGE15_CHECK(entries(grid) == baseline && !undo->canUndo(),
                  "fixture must load three exact ASS entries with a clean undo baseline; got" << entries(grid));

    window->requestActivate();
    STAGE15_CHECK(QTest::qWaitForWindowActive(window, 2000), "main window did not activate");
    auto *gridView = findGridView(window->contentItem(), grid);
    auto *editor = window->findChild<QQuickItem *>(QStringLiteral("editBoxTextArea"));
    STAGE15_CHECK(gridView && editor, "could not find the production grid or EditBox");
    gridView->forceActiveFocus();
    STAGE15_CHECK(gridView->hasActiveFocus(), "grid did not take keyboard focus" << window->activeFocusItem());

    auto key = [window](Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(window, code, modifiers);
        QCoreApplication::processEvents();
    };
    constexpr auto ctrl = Qt::ControlModifier;
    grid->selectSingle(0);
    grid->toggleSelect(2);
    const QString selectedAss = first + QLatin1Char('\n') + third;
    auto clipboardText = [clipboard] {
        return clipboard->text().replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    };
    clipboard->clear();
    key(Qt::Key_C, ctrl);
    STAGE15_CHECK(clipboardText() == selectedAss && entries(grid) == baseline && !undo->canUndo(),
                  "grid Ctrl+C must copy both full ASS entries in document order without a commit");
    clipboard->clear();
    key(Qt::Key_X, ctrl);
    STAGE15_CHECK(clipboardText() == selectedAss && entries(grid) == QStringList{second} && undo->canUndo(),
                  "grid Ctrl+X must copy and remove both selected rows");
    key(Qt::Key_V, ctrl);
    const QStringList pasted{second, first, third};
    STAGE15_CHECK(entries(grid) == pasted && grid->selectionCount() == 2 && grid->activeRow() == 1,
                  "grid Ctrl+V must insert both ASS entries after the active row and select them");
    key(Qt::Key_Z, ctrl);
    STAGE15_CHECK(entries(grid) == QStringList{second} && undo->canUndo(),
                  "first Ctrl+Z must undo the paste only");
    key(Qt::Key_Z, ctrl);
    STAGE15_CHECK(entries(grid) == baseline && !undo->canUndo() && !document->dirty(),
                  "second Ctrl+Z must restore both cut rows in their original order and the clean state");

    grid->selectSingle(1);
    editor->forceActiveFocus();
    STAGE15_CHECK(window->activeFocusItem() == editor, "EditBox did not take keyboard focus");
    const auto body = QStringLiteral("needle second");
    key(Qt::Key_A, ctrl);
    key(Qt::Key_C, ctrl);
    STAGE15_CHECK(clipboardText() == body && entries(grid) == baseline && grid->selectionCount() == 1,
                  "EditBox Ctrl+A/C must select and copy plain text, not ASS rows");
    key(Qt::Key_X, ctrl);
    STAGE15_CHECK(clipboardText() == body && grid->rowCount() == 3
                      && grid->data(grid->index(1, 0), SubtitleGridModel::TextRole).toString().isEmpty(),
                  "EditBox Ctrl+X must clear the selected text without cutting a row");
    key(Qt::Key_V, ctrl);
    STAGE15_CHECK(entries(grid) == baseline, "EditBox Ctrl+V must restore text without pasting rows");
    // Isolate timing undo from text-edit history.
    STAGE15_CHECK(document->openLocalPath(fixture.fileName()), document->lastError());
    gridView->forceActiveFocus();
    grid->selectSingle(0);
    search->setQuery(QStringLiteral("needle"));
    key(Qt::Key_F3);
    STAGE15_CHECK(grid->activeRow() == 1 && grid->selectionCount() == 1 && !undo->canUndo(),
                  "F3 must select the next matching row without editing the document");

    STAGE15_CHECK(!preferencesWindow(window), "Preferences already exists before pressing Alt+O");
    key(Qt::Key_O, Qt::AltModifier);
    auto *preferences = preferencesWindow(window);
    STAGE15_CHECK(preferences && preferences->isVisible(), "Alt+O did not open the Preferences window");
    preferences->close();
    window->requestActivate();
    STAGE15_CHECK(QTest::qWaitForWindowActive(window, 2000), "main window did not regain focus");
    gridView->forceActiveFocus();

    video->setAutoSeekToActiveLine(false);
    STAGE15_CHECK(video->openDummyVideo(320, 180, 25, 100) && video->hasVideo()
                      && video->frameCount() == 100, "could not open dummy video:" << video->lastError());
    constexpr int frame = 25;
    video->seekFrame(frame);
    STAGE15_CHECK(video->currentFrame() == frame, "dummy video did not seek to frame 25");
    const int snapStart = agi::Time(video->timeAtFrameMs(frame, false));
    const int snapEnd = agi::Time(video->timeAtFrameMs(frame, true));
    auto timesMatch = [grid](int start, int end) {
        const auto *line = grid->dialogueAt(1);
        return line && int(line->Start) == start && int(line->End) == end;
    };
    STAGE15_CHECK(timesMatch(600, 3000) && 600 < snapStart && snapStart < snapEnd && snapEnd < 3000,
                  "expected the dummy frame strictly inside the matched subtitle interval");
    key(Qt::Key_3, ctrl);
    STAGE15_CHECK(timesMatch(snapStart, 3000) && undo->canUndo(),
                  "Ctrl+3 must snap the start of the selected row");
    key(Qt::Key_4, ctrl);
    STAGE15_CHECK(timesMatch(snapStart, snapEnd), "Ctrl+4 must snap the end of the selected row");
    key(Qt::Key_Z, ctrl);
    STAGE15_CHECK(timesMatch(snapStart, 3000) && undo->canUndo(),
                  "first timing Ctrl+Z must undo only the end snap");
    key(Qt::Key_Z, ctrl);
    STAGE15_CHECK(entries(grid) == baseline && !undo->canUndo() && !document->dirty(),
                  "second timing Ctrl+Z must restore the original times and clean state");

    video->closeVideo();
    qInfo() << "Stage 15 integration passed";
    return 0;
}

#undef STAGE15_CHECK
