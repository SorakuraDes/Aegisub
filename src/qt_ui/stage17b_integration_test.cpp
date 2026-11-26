#include "stage17a_attachments_test.h"
#include "aegisub_core.h"
#include "viewmodels/fonts_collector_vm.h"
#include "viewmodels/theme_vm.h"
#include "viewmodels/translation_service.h"

#include <QClipboard>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QtEndian>
#include <zlib.h>
#include <cmath>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
using stage17a_attachments_test::click;
using stage17a_attachments_test::object;
using stage17a_attachments_test::qmlObject;
using stage17a_attachments_test::visualItem;
using stage17a_attachments_test::writeBytes;

constexpr auto restartEnv = "AEGISUB_STAGE17B_RESTART_DESTINATION";
#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 17B:" << __LINE__ << message; return 1; \
} } while (false)

bool configured(FontsCollectorViewModel &vm, const QString &destination) {
    return vm.property("action").toInt() == 3 && vm.property("destination").toString() == destination;
}

#ifdef Q_OS_WIN
constexpr int timeout = 5000;
constexpr auto fixture = "\xEF\xBB\xBF" R"ASS([Script Info]
ScriptType: v4.00+
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Arial sample)ASS" "\xCD\xB8" R"ASS({\fnNonExistentFont_XYZ}Missing{\r}Arial again
Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\fnStage17B_CommentOnly}Ignored comment
)ASS";

QByteArray readBytes(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Independently read the single expected ZIP member and validate both headers and payload.
bool zipMatches(const QByteArray &zip, const QByteArray &name, const QByteArray &expected) {
    const auto fits = [&](qsizetype p, qsizetype n) { return p >= 0 && p <= zip.size() && n <= zip.size() - p; };
    const auto u16 = [&](qsizetype p) { return qFromLittleEndian<quint16>(zip.constData() + p); };
    const auto u32 = [&](qsizetype p) { return qFromLittleEndian<quint32>(zip.constData() + p); };
    if (zip.size() < 22 || expected.isEmpty()) return false;
    const qsizetype end = zip.size() - 22;
    if (u32(end) != 0x06054b50 || u16(end + 4) || u16(end + 6) || u16(end + 8) != 1 ||
        u16(end + 10) != 1 || u16(end + 20)) return false;
    const qsizetype central = u32(end + 16), centralSize = u32(end + 12);
    if (centralSize < 46 || !fits(central, centralSize) || central + centralSize != end || u32(central) != 0x02014b50)
        return false;
    const auto flags = u16(central + 8), method = u16(central + 10), nameSize = u16(central + 28);
    const auto crc = u32(central + 16), compressed = u32(central + 20), size = u32(central + 24);
    const qsizetype local = u32(central + 42);
    if ((flags & 1) || !(flags & 0x0800) || size != expected.size() ||
        46 + nameSize + u16(central + 30) + u16(central + 32) != centralSize ||
        zip.mid(central + 46, nameSize) != name || !fits(local, 30) || u32(local) != 0x04034b50 ||
        u16(local + 6) != flags || u16(local + 8) != method || u16(local + 26) != nameSize) return false;
    if (!(flags & 8) && (u32(local + 14) != crc || u32(local + 18) != compressed || u32(local + 22) != size))
        return false;
    const qsizetype data = local + 30 + nameSize + u16(local + 28);
    if (!fits(data, compressed) || data + compressed > central || zip.mid(local + 30, nameSize) != name)
        return false;
    if (flags & 8) {
        qsizetype descriptor = data + compressed;
        if (central - descriptor == 16 && u32(descriptor) == 0x08074b50) descriptor += 4;
        if (central - descriptor != 12 || u32(descriptor) != crc ||
            u32(descriptor + 4) != compressed || u32(descriptor + 8) != size) return false;
    } else if (data + compressed != central) {
        return false;
    }
    QByteArray actual;
    if (method == 0 && compressed == size) {
        actual = zip.mid(data, compressed);
    } else if (method == Z_DEFLATED) {
        actual.resize(qsizetype(size) + 1);
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(zip.constData() + data));
        stream.avail_in = compressed;
        stream.next_out = reinterpret_cast<Bytef *>(actual.data());
        stream.avail_out = static_cast<uInt>(actual.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result != Z_STREAM_END || stream.total_out != size || stream.total_in != compressed) return false;
        actual.chop(1);
    } else {
        return false;
    }
    return actual == expected && crc32(0L, reinterpret_cast<const Bytef *>(actual.constData()), size) == crc;
}

QQuickItem *control(QQuickWindow *window, const QString &name) {
    return visualItem(window->contentItem(), name);
}

QString text(const QObject *item) {
    return item ? item->property("text").toString() : QString{};
}

double luminance(const QColor &color) {
    const auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

bool checkButton(QQuickItem *button, const QImage &image) {
    if (!button || !button->isVisible() || !button->isEnabled()) return false;
    auto *label = qobject_cast<QQuickItem *>(object(button->property("contentItem")));
    auto *background = qobject_cast<QQuickItem *>(object(button->property("background")));
    if (!label || !background || text(label).isEmpty()) return false;
    const auto fill = background->property("color").value<QColor>();
    const auto ink = label->property("color").value<QColor>();
    if (!fill.isValid() || !ink.isValid() || fill.alpha() != 255 || ink.alpha() != 255) return false;
    const double a = luminance(fill), b = luminance(ink);
    if ((qMax(a, b) + .05) / (qMin(a, b) + .05) < 4.5) return false;
    const auto pixels = [&](QQuickItem *item) {
        const auto scene = item->mapRectToScene(item->boundingRect());
        const auto scale = image.devicePixelRatio();
        return QRectF(scene.topLeft() * scale, scene.size() * scale).toAlignedRect().intersected(image.rect());
    };
    const auto fillRegion = pixels(background).adjusted(7, 4, -7, -4);
    const auto inkRegion = pixels(label);
    if (fillRegion.isEmpty() || inkRegion.isEmpty()) return false;
    int fillPixels = 0, inkPixels = 0;
    for (int y = fillRegion.top(); y <= fillRegion.bottom(); ++y)
        for (int x = fillRegion.left(); x <= fillRegion.right(); ++x)
            fillPixels += image.pixelColor(x, y).rgba() == fill.rgba();
    for (int y = inkRegion.top(); y <= inkRegion.bottom(); ++y)
        for (int x = inkRegion.left(); x <= inkRegion.right(); ++x) {
            const auto color = image.pixelColor(x, y);
            inkPixels += color.alpha() == 255 &&
                std::abs(color.red() - ink.red()) + std::abs(color.green() - ink.green()) + std::abs(color.blue() - ink.blue()) < 100;
        }
    return fillPixels >= fillRegion.width() * fillRegion.height() / 2 && inkPixels >= 3;
}

bool captureThemes(QQuickWindow *dialog, ThemeViewModel &theme, TranslationService &i18n, const QString &dir) {
    const auto originalDark = theme.darkMode();
    const auto originalLanguage = i18n.preference();
    const auto restore = qScopeGuard([&] {
        theme.setDarkMode(originalDark);
        i18n.setLanguage(originalLanguage);
    });
    auto *background = control(dialog, QStringLiteral("fontsCollectorBackground"));
    if (!background) return false;
    for (bool dark : {false, true}) {
        theme.setDarkMode(dark);
        for (bool chinese : {false, true}) {
            const auto language = chinese ? QStringLiteral("zh_CN") : QStringLiteral("en_US");
            if (!i18n.setLanguage(language)) return false;
            const auto title = chinese ? QStringLiteral("\u5b57\u4f53\u6536\u96c6\u5668") : QStringLiteral("Fonts Collector");
            const auto label = chinese ? QStringLiteral("\u68c0\u67e5\u5b57\u4f53\u53ef\u7528\u6027") : QStringLiteral("Check fonts for availability");
            if (!QTest::qWaitFor([&] {
                return dialog->title() == title && text(control(dialog, QStringLiteral("fontsCollectorAction0"))) == label;
            }, timeout)) {
                qWarning() << "Fonts Collector labels did not update" << language << dialog->title();
                return false;
            }
            QTest::qWait(150);
            const auto image = dialog->grabWindow();
            const auto surface = background->property("color").value<QColor>();
            if (image.isNull() || !surface.isValid() || surface.alpha() != 255 || background->opacity() != 1.0) return false;
            for (const auto &point : {QPoint(1, 1), QPoint(image.width() - 2, 1),
                                      QPoint(1, image.height() - 2), QPoint(image.width() - 2, image.height() - 2)})
                if (!image.rect().contains(point) || image.pixelColor(point).rgba() != surface.rgba()) return false;
            for (const auto *id : {"fontsCollectorStartButton", "fontsCollectorCloseButton", "fontsCollectorCopyLogButton"}) {
                if (!checkButton(control(dialog, QLatin1String(id)), image)) {
                    qWarning() << "Fonts Collector button appearance" << id << language << dark;
                    return false;
                }
            }
            const auto filename = QStringLiteral("fonts-collector-%1-%2.png")
                .arg(chinese ? QStringLiteral("zh") : QStringLiteral("en"), dark ? QStringLiteral("dark") : QStringLiteral("light"));
            if (!dir.isEmpty() && !image.save(QDir(dir).filePath(filename))) return false;
        }
    }
    return true;
}
#endif
} // namespace

int RunStage17BRestartCheck(FontsCollectorViewModel &collector) {
    const auto expected = qEnvironmentVariable(restartEnv);
    CHECK(!expected.isEmpty() && configured(collector, expected),
          "previous ZIP action or Unicode destination not restored" << collector.property("action") << collector.property("destination"));
    qInfo() << "Stage 17B restart passed";
    return 0;
}

int RunStage17BIntegrationTest(QQmlApplicationEngine &engine, DocumentViewModel &document,
                               FontsCollectorViewModel &collector) {
#ifndef Q_OS_WIN
    Q_UNUSED(engine);
    Q_UNUSED(document);
    Q_UNUSED(collector);
    qInfo() << "Stage 17B requires the Windows font backend";
    return 77;
#else
    auto *main = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    auto *theme = qobject_cast<ThemeViewModel *>(object(engine.rootContext()->contextProperty(QStringLiteral("themeVM"))));
    auto *i18n = qobject_cast<TranslationService *>(object(engine.rootContext()->contextProperty(QStringLiteral("i18n"))));
    auto *undo = qobject_cast<UndoManager *>(object(engine.rootContext()->contextProperty(QStringLiteral("undoVM"))));
    CHECK(main && theme && i18n && undo, "missing Main or theme/language/undo context objects");
    CHECK(i18n->setLanguage(QStringLiteral("en_US")), "could not select English for log assertions");
    CHECK(document.newDocument() && document.filePath().isEmpty(), document.lastError());

    auto *menuItem = main->findChild<QObject *>(QStringLiteral("fontsCollectorMenuItem"));
    CHECK(menuItem && QMetaObject::invokeMethod(menuItem, "triggered"), "File menu item not connected");
    auto *dialog = qobject_cast<QQuickWindow *>(object(main->property("fontsCollectorDialog")));
    CHECK(dialog && QTest::qWaitFor([&] { return dialog->isVisible() && dialog->isExposed(); }, timeout),
          "Fonts Collector did not open from File menu");
    const auto closeOnExit = qScopeGuard([&] { dialog->close(); });
    const auto item = [&](const char *id) { return control(dialog, QLatin1String(id)); };
    const auto action = [&](int mode) { return control(dialog, QStringLiteral("fontsCollectorAction%1").arg(mode)); };
    const auto select = [&](int mode) {
        return click(action(mode)) && collector.property("action").toInt() == mode && action(mode)->property("checked").toBool();
    };
    for (int mode = 0; mode != 4; ++mode) {
        auto *radio = action(mode);
        CHECK(radio && radio->isVisible() && radio->isEnabled() == (mode != 2) && !text(radio).isEmpty(),
              "incorrect radio state for unsaved document" << mode);
    }
    CHECK(!collector.property("hasScriptPath").toBool(), "untitled document unexpectedly has a subtitle folder");

    QTemporaryDir scratch(QStringLiteral("stage17b-fixtures-XXXXXX"));
    CHECK(scratch.isValid(), scratch.errorString());
    const QDir dir(scratch.path());
    const auto scriptName = QStringLiteral("\u5b57\u5e55");
    const QDir scriptDir(dir.filePath(scriptName));
    const auto ass = scriptDir.absoluteFilePath(QStringLiteral("fonts.ass"));
    const auto source = QDir(qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows"))).filePath(QStringLiteral("Fonts/arial.ttf"));
    const auto sourceBytes = readBytes(source);
    const auto fontBasename = QFileInfo(source).fileName();
    CHECK(!sourceBytes.isEmpty(), "Arial fixture font is unavailable" << source);
    CHECK(dir.mkdir(scriptName) && writeBytes(ass, fixture) && document.openLocalPath(ass), document.lastError());
    CHECK(action(2)->isEnabled() && collector.property("hasScriptPath").toBool(), "saved subtitle folder is disabled");

    const auto startButton = [&] { return item("fontsCollectorStartButton"); };
    const auto closeButton = [&] { return item("fontsCollectorCloseButton"); };
    const auto log = [&] { return collector.property("logText").toString(); };
    const auto error = [&] { return collector.property("statusText").toString(); };
    const auto busy = [&] { return collector.property("running").toBool(); };
    const auto run = [&] {
        QSignalSpy busyChanges(&collector, SIGNAL(runningChanged()));
        if (busy() || !click(startButton()) || !busyChanges.isValid() || busyChanges.isEmpty()) return false;
        if (busy() && (startButton()->isEnabled() || closeButton()->isEnabled())) return false;
        return QTest::qWaitFor([&] { return !busy() && busyChanges.size() >= 2; }, 10000) &&
               !log().isEmpty() && startButton()->isEnabled() && closeButton()->isEnabled();
    };
    const auto setDestination = [&](const QString &destination) {
        return collector.setProperty("destination", destination) && collector.property("destination").toString() == destination;
    };
    const auto outputFailed = [&] { return collector.property("outputFailed").toBool(); };
    CHECK(closeButton() && select(0) && run() && !error().isEmpty() && !outputFailed(), "check failed" << error() << log());
    CHECK(collector.property("missingCount").toInt() == 1 &&
          log().contains(QStringLiteral("NonExistentFont_XYZ")) && !log().contains(QStringLiteral("Stage17B_CommentOnly")) &&
          log().contains(fontBasename, Qt::CaseInsensitive), "check log did not distinguish found, missing and commented fonts" << log());
    // U+0378 is unassigned; the GDI scanner should report it as a missing Arial glyph.
    CHECK(log().contains(QStringLiteral("missing the following glyphs"), Qt::CaseInsensitive) &&
          log().contains(QChar(0x0378)), "check log did not report the missing Arial glyph" << log());
    auto *logItem = item("fontsCollectorLog");
    CHECK(logItem && logItem->property("readOnly").toBool(), "log is not a read-only UI control");
    CHECK(click(item("fontsCollectorCopyLogButton")) && QGuiApplication::clipboard()->text() == log(), "Copy Log did not copy the current log");
    const auto shots = qEnvironmentVariable("AEGISUB_STAGE17B_SCREENSHOTS");
    CHECK(shots.isEmpty() || QDir().mkpath(shots), "could not create screenshot directory" << shots);
    CHECK(captureThemes(dialog, *theme, *i18n, shots), "Fonts Collector appearance regression");
    // Restoring the language replaces the translated RadioButton delegates.
    // Let their layout settle before using scene coordinates for a click.
    QTest::qWait(150);

    const QDir copyDir(dir.absoluteFilePath(QStringLiteral("\u5b57\u4f53\u6536\u96c6/\u5b50\u76ee\u5f55")));
    const auto copiedFont = copyDir.absoluteFilePath(fontBasename);
    CHECK(select(1), "folder mode did not select" << collector.action() << action(1));
    CHECK(setDestination(copyDir.absolutePath()), "folder destination not accepted");
    CHECK(run() && !outputFailed() && readBytes(copiedFont) == sourceBytes,
          "folder copy failed or changed font bytes" << error() << log());
    CHECK(run() && !outputFailed() && readBytes(copiedFont) == sourceBytes &&
          (log().contains(QStringLiteral("already exists"), Qt::CaseInsensitive) || log().contains(QStringLiteral("skipp"), Qt::CaseInsensitive)),
          "second folder run did not skip duplicate output" << error() << log());
    CHECK(select(2) && run() && !outputFailed() && readBytes(scriptDir.absoluteFilePath(fontBasename)) == sourceBytes &&
          collector.property("destination").toString() == copyDir.absolutePath(),
          "script-folder copy failed or changed the custom destination" << error() << log());

    const auto archive = dir.absoluteFilePath(QStringLiteral("\u5b57\u4f53\u6253\u5305.zip"));
    CHECK(select(3) && setDestination(archive) && run() && !outputFailed(), "ZIP collection failed" << error() << log());
    auto zipBytes = readBytes(archive);
    CHECK(zipMatches(zipBytes, fontBasename.toUtf8(), sourceBytes), "invalid ZIP structure, CRC, filename or font content");
    // The existing archive must remain distinguishable from a newly generated ZIP.
    zipBytes[zipBytes.size() - 2] = 4;
    zipBytes.append("keep");
    CHECK(writeBytes(archive, zipBytes), "could not add a comment to the archive before the failure test");
    {
        // Deny archive replacement while allowing reads; existing content must survive the error.
        const auto native = QDir::toNativeSeparators(archive).toStdWString();
        const auto handle = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE, "could not lock archive" << GetLastError());
        const auto unlock = qScopeGuard([&] { CloseHandle(handle); });
        CHECK(run() && outputFailed() && !error().isEmpty(), "locked ZIP destination did not report output failure" << error() << log());
    }
    CHECK(readBytes(archive) == zipBytes, "failed ZIP replacement altered the existing archive");
    CHECK(!document.dirty() && !undo->canUndo() && !undo->canRedo() && document.filePath() == ass,
          "font collection modified the open subtitle or undo state");
    CHECK(click(closeButton()) && QTest::qWaitFor([&] { return !dialog->isVisible(); }, timeout), "Close did not dismiss Fonts Collector");

    QString optionError;
    CHECK(configured(collector, archive) && aegisub_qt::FlushOptions(&optionError), optionError);
    const auto config = QJsonDocument::fromJson(readBytes(aegisub_qt::OptionsFilePath())).object();
    CHECK(config.value(QStringLiteral("Tool")).toObject().value(QStringLiteral("Fonts Collector")).toObject().value(QStringLiteral("Action")).toInt() == 3 &&
          config.value(QStringLiteral("Path")).toObject().value(QStringLiteral("Fonts Collector Destination")).toString() == archive,
          "Fonts Collector did not persist the original configuration keys");
    auto childEnv = QProcessEnvironment::systemEnvironment();
    childEnv.insert(QLatin1String(restartEnv), archive);
    QProcess child;
    child.setProcessEnvironment(childEnv);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--stage17b-restart")});
    CHECK(child.waitForFinished(15000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
          "configuration restart failed" << child.errorString() << child.readAllStandardError() << child.readAllStandardOutput());
    CHECK(child.readAllStandardOutput().contains("Stage 17B restart passed"), "child skipped the restart check");
    qInfo() << "Stage 17B Fonts Collector passed";
    return 0;
#endif
}

#undef CHECK
