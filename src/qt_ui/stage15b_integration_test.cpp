#include "viewmodels/subtitle_grid_model.h" // agi::signal before Qt macros
#include "viewmodels/audio_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/undo_manager.h"
#include "viewmodels/video_vm.h"
#include "ass_dialogue.h"
#include "ass_syntax_highlighter.h"
#include "video_viewport_item.h"
#include "visual_overlay_item.h"

#include <QClipboard>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QTest>
#include <cmath>
#include <utility>

namespace {
#define CHECK(condition, message) do { if (!(condition)) { \
    qCritical() << "Stage 15B integration:" << __LINE__ << message; return 1; \
} } while (false)

constexpr auto syntax = "Subtitle/Highlight/Syntax";
constexpr auto font = "Subtitle/Edit Box/Font Size";
constexpr auto follow = "Audio/Lock Scroll on Cursor";
constexpr auto sync = "Video/Subtitle Sync";
constexpr auto levels = "Limits/Undo Levels";
constexpr auto languageKey = "App/Language";
const auto english = QStringLiteral("en_US");

struct CatalogSample {
    QString code;
    QString language;
    QString preferences;
};

// Known catalog strings: an unexpected English fallback must fail the test.
const CatalogSample englishSample{english, QStringLiteral("Language"), QStringLiteral("Preferences")};
const CatalogSample samples[] = {
    {QStringLiteral("zh_CN"), QStringLiteral("\u8bed\u8a00"), QStringLiteral("\u504f\u597d")},
    {QStringLiteral("de"), QStringLiteral("Sprache"), QStringLiteral("Einstellungen")},
    {QStringLiteral("fr_FR"), QStringLiteral("Langue"), QStringLiteral("Pr\u00e9f\u00e9rences")},
};
constexpr int fontSize = 19;
constexpr int undoLevels = 2;
constexpr int interfacePage = 4;
const QString tagged = QStringLiteral("{\\pos(160,90)\\b1}Zebra");

QObject *object(const QVariant &value) {
    if (auto *obj = value.value<QObject *>()) return obj;
    return value.value<QJSValue>().toQObject();
}

template<class T = QObject>
T *vm(QQmlApplicationEngine &engine, const char *name) {
    return qobject_cast<T *>(object(engine.rootContext()->contextProperty(QLatin1String(name))));
}

QQuickWindow *root(QQmlApplicationEngine &engine) {
    return qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
}

QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *match = visualChild(child, name)) return match;
    return nullptr;
}

QQuickItem *control(QQuickWindow *window, const char *name) {
    return visualChild(window->contentItem(), QString::fromLatin1(name));
}

bool image(QQuickWindow *window, const char *name) {
    const auto artifacts = qEnvironmentVariable("AEGISUB_QT_TEST_ARTIFACTS");
    if (artifacts.isEmpty()) return true;
    QTest::qWait(100);
    return QDir().mkpath(artifacts) && window->grabWindow().save(QDir(artifacts).filePath(QString::fromLatin1(name)));
}

bool page(QQuickWindow *window, int index) {
    auto *nav = control(window, "preferencesNavigation");
    if (!nav) return false;
    nav->forceActiveFocus();
    for (int i = 0; i < 7 && window->property("page").toInt() != index; ++i)
        QTest::keyClick(window, window->property("page").toInt() < index ? Qt::Key_Down : Qt::Key_Up);
    QCoreApplication::processEvents();
    return window->property("page").toInt() == index;
}

bool toggle(QQuickWindow *window, const char *key, bool value) {
    auto *item = control(window, key);
    if (!item) return false;
    item->forceActiveFocus();
    if (item->property("checked").toBool() == value) QTest::keyClick(window, Qt::Key_Space);
    QTest::keyClick(window, Qt::Key_Space);
    return item->property("checked").toBool() == value;
}

bool number(QQuickWindow *window, const char *key, int value) {
    auto *item = control(window, key);
    if (!item) return false;
    item->forceActiveFocus();
    const int delta = value - item->property("value").toInt();
    if (std::abs(delta) > 100) return false;
    for (int i = 0; i < std::abs(delta); ++i)
        QTest::keyClick(window, delta > 0 ? Qt::Key_Up : Qt::Key_Down);
    return item->property("value").toInt() == value;
}

QVariant jsonKey(const QJsonObject &root, const char *key) {
    QJsonValue result(root);
    for (const auto &part : QString::fromLatin1(key).split('/')) result = result.toObject().value(part);
    return result.toVariant();
}

QObject *menu(QObject *owner, const char *title) {
    for (auto *obj : owner->findChildren<QObject *>())
        if (obj->property("count").isValid() && obj->property("title").toString() == QLatin1String(title)) return obj;
    return nullptr;
}

QObject *at(QObject *menu, int index) {
    if (!menu) return nullptr;
    QQmlExpression expression(qmlContext(menu), menu, QStringLiteral("itemAt(%1)").arg(index));
    return object(expression.evaluate());
}

QObject *entry(QObject *menu, const char *name) {
    if (!menu) return nullptr;
    for (int i = 0; i < menu->property("count").toInt(); ++i)
        if (auto *obj = at(menu, i); obj && obj->property("text").toString() == QLatin1String(name)) return obj;
    return nullptr;
}

bool translatedMenus(QQuickWindow *window) {
    auto *bar = window->findChild<QObject *>(QStringLiteral("mainMenuBar"));
    const QStringList titles{QStringLiteral("文件"), QStringLiteral("编辑"), QStringLiteral("字幕"),
                             QStringLiteral("时间"), QStringLiteral("视频"), QStringLiteral("音频"),
                             QStringLiteral("自动化"), QStringLiteral("视图"), QStringLiteral("帮助")};
    if (!bar) {
        qWarning() << "Stage 15B menu bar: mainMenuBar missing";
        return false;
    }
    QObject *subtitle = nullptr;
    QStringList actual;
    for (int i = 0; i < bar->property("count").toInt(); ++i) {
        QQmlExpression expression(qmlContext(bar), bar, QStringLiteral("menuAt(%1)").arg(i));
        auto *top = object(expression.evaluate());
        actual << (top ? top->property("title").toString() : QStringLiteral("<missing>"));
        if (i == 2) subtitle = top;
    }
    if (actual != titles) {
        qWarning() << "Stage 15B menu bar: expected" << titles << "but found" << actual;
        return false;
    }

    const auto labels = [](QObject *menu) {
        QStringList result;
        if (menu) {
            for (int i = 0; i < menu->property("count").toInt(); ++i) {
                const auto *item = at(menu, i);
                if (item && item->property("text").isValid())
                    result.append(item->property("text").toString());
            }
        }
        return result;
    };
    const QStringList direct{QStringLiteral("样式管理器…"), QStringLiteral("重设分辨率…"), QStringLiteral("插入行"),
        QStringLiteral("重复行"), QStringLiteral("以当前帧前分割行"), QStringLiteral("以当前帧后分割行"),
        QStringLiteral("删除行"), QStringLiteral("合并行"), QStringLiteral("展开所有折叠"),
        QStringLiteral("收起所有折叠"), QStringLiteral("清除所有折叠"), QStringLiteral("排序所有行"),
        QStringLiteral("排序选择行"), QStringLiteral("互换行"), QStringLiteral("选择多行…"), QStringLiteral("全选")};
    if (labels(subtitle) != direct) return false;

    const auto submenu = [subtitle](const QString &title) -> QObject * {
        for (int i = 0; i < subtitle->property("count").toInt(); ++i) {
            auto *item = at(subtitle, i);
            if (item && item->property("text").toString() == title)
                return object(item->property("subMenu"));
        }
        return nullptr;
    };
    const QStringList insert{QStringLiteral("当前行之前"), QStringLiteral("当前行之后"),
                             QStringLiteral("当前行之前，以视频时间起始"), QStringLiteral("当前行之后，以视频时间起始")};
    const QStringList join{QStringLiteral("连接"), QStringLiteral("保留首行")};
    const QStringList sort{QStringLiteral("开始时间"), QStringLiteral("结束时间"), QStringLiteral("样式名称"),
                           QStringLiteral("说话人"), QStringLiteral("特效"), QStringLiteral("文本"),
                           QStringLiteral("去除标签的文本"), QStringLiteral("层级")};
    return labels(submenu(direct[2])) == insert && labels(submenu(direct[7])) == join &&
           labels(submenu(QStringLiteral("排序所有行"))) == sort && labels(submenu(QStringLiteral("排序选择行"))) == sort;
}

// Invoke the production menu item's signal without relying on the platform's popup focus.
bool trigger(QObject *item) {
    if (!item || !item->property("enabled").toBool() || !QMetaObject::invokeMethod(item, "triggered")) return false;
    QCoreApplication::processEvents();
    return true;
}

bool trigger(QObject *menu, const char *name) { return trigger(entry(menu, name)); }

QObject *languageMenuItem(QQuickWindow *window) {
    return window->findChild<QObject *>(QStringLiteral("languageMenuItem"));
}

bool selectLanguage(QQuickItem *picker, const QString &code) {
    if (!picker || !picker->isVisible() || !picker->isEnabled()) return false;
    QQmlExpression index(qmlContext(picker), picker, QStringLiteral("indexOfValue('%1')").arg(code));
    bool ok = false;
    const int row = index.evaluate().toInt(&ok);
    return !index.hasError() && ok && row >= 0 && picker->setProperty("currentIndex", row) &&
        picker->property("currentValue").toString() == code;
}

bool setLanguageFromView(QQuickWindow *window, const QString &from, const QString &to) {
    auto *dialog = window->findChild<QObject *>(QStringLiteral("languageDialog"));
    if (!dialog || !trigger(languageMenuItem(window)) ||
        !QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, 2000)) return false;
    auto *picker = control(window, "languagePicker");
    auto *apply = control(window, "languageApplyButton");
    return picker && picker->property("currentValue").toString() == from && selectLanguage(picker, to) &&
        apply && apply->isVisible() && apply->isEnabled() && QMetaObject::invokeMethod(apply, "clicked") &&
        QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 2000);
}

bool languageMatches(QQuickWindow *window, TranslationService *i18n, const CatalogSample &sample) {
    auto *item = languageMenuItem(window);
    return item && i18n->language() == sample.code && i18n->preference() == sample.code &&
        item->property("text").toString() == sample.language + QStringLiteral("...");
}

bool compactTransport(QQuickWindow *window, const char *name) {
    const auto *button = control(window, name);
    const auto *row = button ? button->parentItem() : nullptr;
    const auto *strip = row ? row->parentItem() : nullptr;
    constexpr qreal tolerance = 0.5;
    return strip && button->isVisible() && button->isEnabled() && button->width() > 0 && button->height() > 0 &&
        qAbs(strip->height() - 36) < tolerance && button->height() <= strip->height() + tolerance &&
        qAbs(row->y() + button->y() + button->height() / 2 - strip->height() / 2) < tolerance &&
        row->x() + button->x() >= -tolerance && row->x() + button->x() + button->width() <= strip->width() + tolerance;
}

QString text(SubtitleGridModel *grid, int row) {
    const auto *line = grid->dialogueAt(row);
    return line ? QString::fromStdString(line->Text.get()) : QString();
}

bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
}

bool translationFallback() {
    QTemporaryDir fixture(QStringLiteral("qt-mnemonic-catalog-XXXXXX"));
    const auto localeDir = QStringLiteral("es/LC_MESSAGES");
    if (!fixture.isValid() || !QDir(fixture.path()).mkpath(localeDir)) return false;

    const QByteArray ids[]{"&Exact", "Exact", "Vie&w && &Act"};
    const QByteArray translations[]{"Fallback(&F)", "Exact && unchanged", "&Ver && actuar(&A)..."};
    constexpr quint32 count = 3, originals = 28, translated = originals + count * 8;
    QByteArray mo, strings;
    QDataStream stream(&mo, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x950412de) << quint32(0) << count << originals << translated << quint32(0) << quint32(0);
    for (const auto *table : {ids, translations}) {
        for (quint32 i = 0; i < count; ++i) {
            const auto &item = table[i];
            stream << quint32(item.size()) << (translated + count * 8 + quint32(strings.size()));
            strings += item;
            strings += '\0';
        }
    }
    stream.writeRawData(strings.constData(), int(strings.size()));
    if (stream.status() != QDataStream::Ok ||
        !writeFile(QDir(fixture.path()).filePath(localeDir + QStringLiteral("/aegisub.mo")), mo)) return false;

    const auto previousDir = qgetenv("AEGISUB_PO_DIR");
    const auto previousLanguage = qgetenv("AEGISUB_QT_LANGUAGE");
    qputenv("AEGISUB_PO_DIR", fixture.path().toUtf8());
    qputenv("AEGISUB_QT_LANGUAGE", "es");
    const TranslationService test;
    if (previousDir.isNull()) qunsetenv("AEGISUB_PO_DIR");
    else qputenv("AEGISUB_PO_DIR", previousDir);
    if (previousLanguage.isNull()) qunsetenv("AEGISUB_QT_LANGUAGE");
    else qputenv("AEGISUB_QT_LANGUAGE", previousLanguage);

    return test.language() == QStringLiteral("es") &&
           test.t(QStringLiteral("Exact")) == QStringLiteral("Exact & unchanged") &&
           test.t(QStringLiteral("&Exact")) == QStringLiteral("Fallback") &&
           test.t(QStringLiteral("View & Act")) == QStringLiteral("Ver & actuar...") &&
           test.t(QStringLiteral("Unknown & literal")) == QStringLiteral("Unknown & literal");
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool assFixture(const QString &path, const QByteArray &resolution = "PlayResX: 320\nPlayResY: 180\n",
                const QByteArray &firstText = "{\\pos(160,90)\\b1}Zebra", int extraRows = 0) {
    QByteArray data = R"ASS([Script Info]
ScriptType: v4.00+
PlayResX: 320
PlayResY: 180
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 2,0:00:00.40,0:00:02.80,Default,Zed,0,0,0,,{\pos(160,90)\b1}Zebra
Dialogue: 0,0:00:00.60,0:00:03.00,Default,Mia,0,0,0,,Anchor
Dialogue: 1,0:00:01.00,0:00:04.00,Default,Ada,0,0,0,,Apple
)ASS";
    data.replace("PlayResX: 320\nPlayResY: 180\n", resolution);
    data.replace("{\\pos(160,90)\\b1}Zebra", firstText);
    for (int i = 0; i < extraRows; ++i)
        data += "Dialogue: 0,0:00:00.00,0:00:00.30,Default,,0,0,0,,Scroll fixture\n";
    return writeFile(path, data);
}

bool videoFixture(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray header("YUV4MPEG2 W32 H18 F25:1 Ip A1:1 C420jpeg\n");
    if (file.write(header) != header.size()) return false;
    QByteArray frame("FRAME\n");
    frame += QByteArray(32 * 18, '\x50');
    frame += QByteArray(32 * 18 / 2, '\x80');
    for (int i = 0; i < 5; ++i)
        if (file.write(frame) != frame.size()) return false;
    return true;
}

bool waveFixture(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 rate = 8000, bytes = rate * 2;
    out.writeRawData("RIFF", 4);
    out << quint32(36 + bytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << rate << bytes << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << bytes;
    const QByteArray silence(bytes, '\0');
    return out.writeRawData(silence.constData(), silence.size()) == silence.size() && out.status() == QDataStream::Ok;
}

QByteArray aviChunk(const char *type, const QByteArray &payload) {
    QByteArray result;
    QDataStream out(&result, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData(type, 4);
    out << quint32(payload.size());
    out.writeRawData(payload.constData(), payload.size());
    if (payload.size() % 2) out << quint8(0);
    return result;
}

// Two uncompressed streams in a tiny, indexed AVI. No encoder or external fixture is needed.
bool avFixture(const QString &path, quint16 width, quint16 height, quint32 frames, char pcmByte) {
    constexpr quint32 fps = 25, rate = 48000, samples = rate / fps;
    const QByteArray image(((width * 3 + 3) & ~3) * height, '\x50');
    const QByteArray audio(samples * 2, pcmByte);
    const quint32 videoBytes = image.size(), audioBytes = audio.size();
    QByteArray mainHeader, videoHeader, audioHeader, bitmap, wave, index;
    QDataStream avih(&mainHeader, QIODevice::WriteOnly), video(&videoHeader, QIODevice::WriteOnly),
        sound(&audioHeader, QIODevice::WriteOnly), dib(&bitmap, QIODevice::WriteOnly),
        pcm(&wave, QIODevice::WriteOnly), idx(&index, QIODevice::WriteOnly);
    for (auto *stream : {&avih, &video, &sound, &dib, &pcm, &idx})
        stream->setByteOrder(QDataStream::LittleEndian);
    avih << quint32(1000000 / fps) << quint32((videoBytes + audioBytes) * fps) << quint32(0) << quint32(0x110)
         << frames << quint32(0) << quint32(2) << qMax(videoBytes, audioBytes) << quint32(width) << quint32(height)
         << quint32(0) << quint32(0) << quint32(0) << quint32(0);
    video.writeRawData("vidsDIB ", 8);
    sound.writeRawData("auds\0\0\0\0", 8);
    const auto streamHeader = [](QDataStream &out, quint32 scale, quint32 frequency, quint32 length,
                                 quint32 buffer, quint32 sampleSize, quint16 w, quint16 h) {
        out << quint32(0) << quint16(0) << quint16(0) << quint32(0) << scale << frequency << quint32(0)
            << length << buffer << quint32(0xffffffff) << sampleSize << quint16(0) << quint16(0) << w << h;
    };
    streamHeader(video, 1, fps, frames, videoBytes, 0, width, height);
    streamHeader(sound, 2, rate * 2, frames * samples, audioBytes, 2, 0, 0);
    dib << quint32(40) << qint32(width) << qint32(height) << quint16(1) << quint16(24) << quint32(0)
        << videoBytes << quint32(0) << quint32(0) << quint32(0) << quint32(0);
    pcm << quint16(1) << quint16(1) << rate << quint32(rate * 2) << quint16(2) << quint16(16);
    const auto headers = aviChunk("LIST", QByteArray("hdrl") + aviChunk("avih", mainHeader)
        + aviChunk("LIST", QByteArray("strl") + aviChunk("strh", videoHeader) + aviChunk("strf", bitmap))
        + aviChunk("LIST", QByteArray("strl") + aviChunk("strh", audioHeader) + aviChunk("strf", wave)));
    QByteArray movi("movi");
    const auto append = [&](const char *type, quint32 flags, const QByteArray &payload) {
        idx.writeRawData(type, 4);
        idx << flags << quint32(movi.size()) << quint32(payload.size());
        movi += aviChunk(type, payload);
    };
    for (quint32 i = 0; i < frames; ++i) {
        append("00db", 0x10, image);
        append("01wb", 0x10, audio);
    }
    return writeFile(path, aviChunk("RIFF", QByteArray("AVI ") + headers + aviChunk("LIST", movi) + aviChunk("idx1", index)));
}

bool consumers(QQmlApplicationEngine &engine, QQuickWindow *window) {
    auto *prefs = vm<PreferencesViewModel>(engine, "preferencesVM");
    auto *audio = vm<AudioViewModel>(engine, "audioVM");
    auto *video = vm<VideoViewModel>(engine, "videoVM");
    auto *editor = control(window, "editBoxTextArea");
    auto *highlighter = window->findChild<AssSyntaxHighlighter *>();
    return prefs && audio && video && editor && highlighter && !highlighter->target() &&
           !prefs->syntaxHighlighting() && editor->property("font").value<QFont>().pointSize() == fontSize &&
           audio->followPlayhead() && video->autoSeekToActiveLine() && prefs->optionValues().value(levels).toInt() == undoLevels;
}
}

int runStage15BReloadTest(QQmlApplicationEngine &engine) {
    auto *window = root(engine);
    auto *prefs = vm<PreferencesViewModel>(engine, "preferencesVM");
    auto *mru = vm(engine, "mruVM");
    CHECK(window && prefs && mru && consumers(engine, window), "restart did not restore preferences to consumers");
    CHECK(prefs->preferencePage() == interfacePage, "last preferences page did not restore");
    auto *i18n = vm<TranslationService>(engine, "i18n");
    const auto code = qEnvironmentVariable("STAGE15B_LANGUAGE");
    const CatalogSample *sample = code == english ? &englishSample : nullptr;
    for (const auto &candidate : samples)
        if (candidate.code == code) sample = &candidate;
    CHECK(i18n && sample && languageMatches(window, i18n, *sample), "language did not restore before QML creation" << code);
    if (code == QStringLiteral("zh_CN")) {
        CHECK(translatedMenus(window), "Chinese menu bar or Subtitle descendants did not restore");
    }
    for (auto pair : {std::pair{"subtitles", "STAGE15B_RECENT_SUBTITLE"}, std::pair{"audio", "STAGE15B_RECENT_AUDIO"},
                      std::pair{"videos", "STAGE15B_RECENT_VIDEO"}}) {
        const auto expected = qEnvironmentVariable(pair.second);
        CHECK(!expected.isEmpty() && mru->property(pair.first).toStringList().value(0) == expected, "MRU did not restore" << pair.first);
    }
    qInfo() << "Stage 15B restart passed";
    return 0;
}

int runStage15BIntegrationTest(QQmlApplicationEngine &engine) {
    auto *window = root(engine);
    auto *prefs = vm<PreferencesViewModel>(engine, "preferencesVM");
    auto *document = vm<DocumentViewModel>(engine, "documentVM");
    auto *grid = vm<SubtitleGridModel>(engine, "gridModel");
    auto *undo = vm<UndoManager>(engine, "undoVM");
    auto *audio = vm<AudioViewModel>(engine, "audioVM");
    auto *video = vm<VideoViewModel>(engine, "videoVM");
    auto *mru = vm(engine, "mruVM");
    auto *i18n = vm<TranslationService>(engine, "i18n");
    CHECK(window && prefs && document && grid && undo && audio && video && mru && i18n, "root or context missing");
    CHECK(!i18n->forced() && languageMatches(window, i18n, englishSample), "missing isolated English baseline");
    CHECK(translationFallback(), "exact-first mnemonic fallback did not preserve literal ampersands");
    auto *languageAction = entry(menu(window, "View"), "Language...");
    CHECK(languageAction && languageAction == languageMenuItem(window), "Language action is not under View");
    const auto languages = i18n->availableLanguages();
    const auto available = [&languages](const QString &code) {
        for (const auto &value : languages) {
            const auto language = value.toMap();
            if (language.value(QStringLiteral("code")).toString() == code)
                return !language.value(QStringLiteral("name")).toString().isEmpty();
        }
        return false;
    };
    CHECK(available(english), "English fallback not offered in language selectors");
    const CatalogSample *localized = nullptr;
    for (const auto &sample : samples) {
        if (available(sample.code)) {
            localized = &sample;
            break;
        }
    }
    if (localized) {
        CHECK(setLanguageFromView(window, english, localized->code) && languageMatches(window, i18n, *localized),
              "View Language did not apply the loaded catalog live" << localized->code << i18n->error());
        if (localized->code == QStringLiteral("zh_CN")) {
            CHECK(translatedMenus(window), "Chinese menu bar or Subtitle descendants did not retranslate live");
        }
    } else {
        qInfo() << "Stage 15B: zh_CN, de and fr_FR catalogs unavailable; skipping non-English switching";
    }
    QTemporaryDir fixture(QStringLiteral("stage15b-fixture-XXXXXX"));
    CHECK(fixture.isValid(), fixture.errorString());
    struct CloseMedia {
        AudioViewModel *audio;
        VideoViewModel *video;
        ~CloseMedia() { audio->closeAudio(); video->closeVideo(); }
    } closeMedia{audio, video};
    const auto ass = QDir(fixture.path()).absoluteFilePath(QStringLiteral("\u5b57\u5e55.ass"));
    const auto wav = QDir(fixture.path()).absoluteFilePath(QStringLiteral("\u97f3\u9891.wav"));
    const auto y4m = QDir(fixture.path()).absoluteFilePath(QStringLiteral("\u89c6\u9891.y4m"));
    CHECK(assFixture(ass) && waveFixture(wav) && videoFixture(y4m), "fixture write failed");
    video->closeVideo();
    CHECK(document->openLocalPath(ass) && grid->rowCount() == 3, "Unicode ASS open failed");

    window->requestActivate();
    QTest::qWait(50);
    QTest::keyClick(window, Qt::Key_O, Qt::AltModifier);
    QTest::qWait(100);
    auto *settings = qobject_cast<QQuickWindow *>(object(window->property("preferencesWindow")));
    CHECK(settings && settings->isVisible(), "Alt+O did not open Preferences");
    CHECK(page(settings, 0), "cannot open Preferences General");
    auto *languagePicker = control(settings, "preferencesLanguagePicker");
    const auto &initial = localized ? *localized : englishSample;
    CHECK(languagePicker && languagePicker->isVisible() && languagePicker->isEnabled() &&
          languagePicker->property("currentValue").toString() == initial.code && settings->title() == initial.preferences,
          "Preferences General did not reflect the current language" << initial.code);
    if (localized) {
        // Exercise the QML handler directly, as with the menu commands, to
        // avoid native popup focus differences on the offscreen platform.
        CHECK(selectLanguage(languagePicker, english) &&
              QMetaObject::invokeMethod(languagePicker, "activated", Q_ARG(int, languagePicker->property("currentIndex").toInt())) &&
              languageMatches(window, i18n, englishSample) && settings->title() == englishSample.preferences,
              "Preferences General did not retranslate both open windows" << i18n->error());
    }
    CHECK(number(settings, levels, undoLevels), "cannot set Undo Levels");
    CHECK(page(settings, 2) && toggle(settings, follow, true) && audio->followPlayhead(), "follow not live");
    audio->setFollowPlayhead(false);
    CHECK(!control(settings, follow)->property("checked").toBool() && toggle(settings, follow, true), "follow not bidirectional");
    CHECK(page(settings, 3) && toggle(settings, sync, true) && video->autoSeekToActiveLine(), "video sync not live");
    video->setAutoSeekToActiveLine(false);
    CHECK(!control(settings, sync)->property("checked").toBool() && toggle(settings, sync, true), "sync not bidirectional");
    CHECK(page(settings, interfacePage) && toggle(settings, syntax, false) && number(settings, font, fontSize), "interface controls failed");
    CHECK(consumers(engine, window) && image(settings, "preferences-interface.png"), "live interface settings or screenshot failed");
    settings->close();
    QFile config(prefs->configFilePath());
    CHECK(config.open(QIODevice::ReadOnly), "config.json missing");
    const auto json = QJsonDocument::fromJson(config.readAll()).object();
    config.close();
    CHECK(jsonKey(json, syntax) == QVariant(false) && jsonKey(json, follow) == QVariant(true) && jsonKey(json, sync) == QVariant(true) &&
          jsonKey(json, font).toInt() == fontSize && jsonKey(json, levels).toInt() == undoLevels &&
          jsonKey(json, languageKey).toString() == english && jsonKey(json, "Tool/Preferences/Page").toInt() == interfacePage,
          "config.json did not persist real keys");

    grid->selectSingle(0);
    grid->toggleSelect(2);
    for (int i = 0; i < 4; ++i) grid->swapSelectedLines();
    int count = 0;
    for (; undo->canUndo() && count < 5; ++count) undo->undo();
    CHECK(count == undoLevels, "live undo cap not applied" << count);
    CHECK(document->openLocalPath(ass), "cannot reset fixture");

    auto *recentAss = window->findChild<QObject *>(QStringLiteral("recentSubtitlesMenu"));
    auto *recentWav = window->findChild<QObject *>(QStringLiteral("recentAudioMenu"));
    auto *empty = at(window->findChild<QObject *>(QStringLiteral("recentVideosMenu")), 0);
    CHECK(empty && !empty->property("enabled").toBool(), "empty Video Recent must be disabled");
    CHECK(document->newDocument() && trigger(at(recentAss, 0)) && document->filePath() == ass && grid->rowCount() == 3,
          "Subtitle Recent did not reopen Unicode filename");
    CHECK(audio->openLocalPath(wav), "Unicode audio fixture failed" << audio->lastError());
    audio->closeAudio();
    CHECK(trigger(at(recentWav, 0)) && audio->hasAudio(), "Audio Recent did not reopen Unicode filename");
    auto *recentVideo = window->findChild<QObject *>(QStringLiteral("recentVideosMenu"));
    CHECK(video->openLocalPath(y4m), "Unicode Y4M fixture failed" << video->lastError());
    video->closeVideo();
    CHECK(trigger(at(recentVideo, 0)) && video->filePath() == y4m, "Video Recent did not reopen Unicode filename");
    const auto transportsFit = [&] {
        return compactTransport(window, "audioPlayLineButton") && compactTransport(window, "videoPlayButton");
    };
    CHECK(QTest::qWaitFor(transportsFit, 2000), "Audio/Video play controls do not fit their 36 px strips in Full view");
    const auto &restart = localized ? *localized : englishSample;
    if (localized) {
        CHECK(setLanguageFromView(window, english, localized->code) && languageMatches(window, i18n, *localized),
              "cannot select the non-English language for restart" << localized->code << i18n->error());
        CHECK(QTest::qWaitFor(transportsFit, 2000), "Localized play controls do not fit their 36 px strips" << localized->code);
    }
    video->closeVideo();
    const auto savedConfig = QJsonDocument::fromJson(readFile(prefs->configFilePath())).object();
    CHECK(jsonKey(savedConfig, languageKey).toString() == restart.code, "App/Language was not written to config.json" << restart.code);

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("STAGE15B_LANGUAGE"), restart.code);
    environment.insert(QStringLiteral("STAGE15B_RECENT_VIDEO"), y4m);
    environment.insert(QStringLiteral("STAGE15B_RECENT_SUBTITLE"), ass);
    environment.insert(QStringLiteral("STAGE15B_RECENT_AUDIO"), wav);
    QProcess child;
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--stage15b-reload")});
    CHECK(child.waitForStarted(5000) && child.waitForFinished(15000), "restart failed to execute" << child.errorString());
    const auto out = child.readAllStandardOutput(), err = child.readAllStandardError();
    CHECK(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 && err.isEmpty() && out.contains("Stage 15B restart passed"),
          "restart failed" << child.exitCode() << out << err);
    qInfo().noquote() << QString::fromUtf8(out).trimmed();
    if (localized) {
        CHECK(setLanguageFromView(window, localized->code, english) && languageMatches(window, i18n, englishSample),
              "cannot restore English after restart test" << i18n->error());
    }

    auto *selected = menu(window, "Sort Selected Lines");
    auto *swap = entry(menu(window, "Subtitle"), "Swap Lines");
    CHECK(selected && selected->property("count").toInt() == 8 && entry(menu(window, "Sort All Lines"), "Stripped Text"), "sort menu incomplete");
    grid->selectSingle(0);
    CHECK(swap && !swap->property("enabled").toBool(), "Swap enabled for one row");
    for (int command = 0; command < 2; ++command) {
        grid->selectSingle(0);
        grid->toggleSelect(2);
        CHECK((command ? trigger(swap) : trigger(selected, "Stripped Text")) && text(grid, 0) == QStringLiteral("Apple") &&
              text(grid, 1) == QStringLiteral("Anchor") && text(grid, 2) == tagged && grid->sortedSelectedRows() == QList<int>({0, 2}),
              "selected sort/swap moved an unselected slot or lost selection" << command);
        undo->undo();
        CHECK(!undo->canUndo() && text(grid, 0) == tagged, "selected sort/swap is not one undo");
    }

    CHECK(video->openDummyVideo(320, 180, 25.0, 250), "dummy video failed");
    grid->selectSingle(0);
    grid->toggleSelect(2);
    video->seekToTimeMs(2040);
    const int delta = video->currentTimeMs() - 400;
    CHECK(trigger(menu(window, "Timing"), "Shift to Current Frame") && static_cast<int>(grid->dialogueAt(0)->Start) == 400 + delta &&
          static_cast<int>(grid->dialogueAt(0)->End) == 2800 + delta && static_cast<int>(grid->dialogueAt(1)->Start) == 600 &&
          static_cast<int>(grid->dialogueAt(2)->Start) == 1000 + delta && static_cast<int>(grid->dialogueAt(2)->End) == 4000 + delta,
          "shift altered relative times or unselected row");
    undo->undo();
    CHECK(!undo->canUndo() && static_cast<int>(grid->dialogueAt(0)->Start) == 400, "shift is not one undo");
    auto *insert = menu(window, "Insert Lines");
    for (int after = 0; after < 2; ++after) {
        grid->selectSingle(1);
        video->seekToTimeMs(2040);
        const int boundary = video->timeAtFrameMs(video->currentFrame(), false);
        CHECK(trigger(insert, after ? "After Current, at Video Time" : "Before Current, at Video Time") && grid->rowCount() == 4 &&
              static_cast<int>(grid->dialogueAt(1 + after)->Start) == boundary, "insert missed intended time or slot");
        undo->undo();
        CHECK(!undo->canUndo() && grid->rowCount() == 3, "insert is not one undo");
    }

    window->requestActivate();
    QTest::qWait(50);
    auto *jump = window->findChild<QObject *>(QStringLiteral("jumpDialog"));
    for (int test = 0; test < 3; ++test) {
        QTest::keyClick(window, Qt::Key_G, Qt::ControlModifier);
        CHECK(jump && QTest::qWaitFor([&] { return jump->property("opened").toBool(); }, 2000),
              "Ctrl+G did not open Jump" << test << jump << window->isActive() << window->activeFocusItem());
        auto *input = control(window, "jumpInput");
        CHECK(input && QTest::qWaitFor([&] { return input->hasActiveFocus(); }, 2000),
              "Jump input did not take focus" << test << input << window->activeFocusItem() << window->isActive());
        const auto value = test == 0 ? QStringLiteral("47") : test == 1 ? QStringLiteral("0:00:03.00") : QStringLiteral("invalid");
        CHECK(input->setProperty("text", value), "cannot fill Jump input");
        if (test == 1) CHECK(image(window, "jump-timecode.png"), "cannot capture Jump");
        const auto expected = test == 0 ? 47 : test == 1 ? 75 : video->currentFrame();
        QTest::keyClick(window, test == 2 ? Qt::Key_Escape : Qt::Key_Return);
        CHECK(QTest::qWaitFor([&] { return !jump->property("visible").toBool() && video->currentFrame() == expected; }, 2000),
              "Jump did not confirm/cancel correctly" << test << video->currentFrame());
    }

    auto *view = menu(window, "View");
    auto *vpLoader = control(window, "videoPanelLoader");
    auto *auLoader = control(window, "audioPanelLoader");
    const char *layout[] = {"Subs Only View", "Video+Subs View", "Audio+Subs View", "Full view"};
    for (int i = 0; i < 4; ++i) {
        CHECK(trigger(view, layout[i]) && window->property("workspaceLayout").toInt() == i, "layout command missing" << i);
        CHECK(vpLoader && auLoader && vpLoader->isVisible() == (i == 1 || i == 3) && auLoader->isVisible() == (i >= 2) &&
              video->hasVideo() && audio->hasAudio(), "layout unloaded media or displayed wrong panels" << i);
        for (int j = 0; j < 4; ++j) {
            auto *row = entry(view, layout[j]);
            CHECK(row && row->property("checked").toBool() == (i == j), "layout checks not exclusive");
        }
    }
    CHECK(prefs->setOption(QStringLiteral("Subtitle/Grid/Hide Overrides Char"), QStringLiteral("~")), "cannot customize tag marker");
    const char *tags[] = {"Show Tags", "Simplify Tags", "Hide Tags"};
    const QString displayed[] = {tagged, QStringLiteral("~Zebra"), QStringLiteral("Zebra")};
    for (int i = 0; i < 3; ++i) {
        CHECK(trigger(view, tags[i]) && grid->data(grid->index(0), SubtitleGridModel::DisplayTextRole).toString() == displayed[i] &&
              text(grid, 0) == tagged && !undo->canUndo(), "tag display changed source or rendered wrong text" << i);
        for (int j = 0; j < 3; ++j) {
            auto *row = entry(view, tags[j]);
            CHECK(row && row->property("checked").toBool() == (i == j), "tag checks not exclusive");
        }
    }

    auto *viewport = qobject_cast<VideoViewportItem *>(control(window, "videoViewport"));
    auto *overlay = qobject_cast<VisualOverlayItem *>(control(window, "visualOverlay"));
    auto *zoom = window->findChild<QObject *>(QStringLiteral("zoomMenu"));
    CHECK(viewport && overlay && overlay->viewport() == viewport && zoom, "video/overlay geometry not shared");
    const char *zooms[] = {"Fit", "50%", "100%", "200%"};
    for (int i = 0; i < 4; ++i) {
        CHECK(trigger(zoom, zooms[i]), "Zoom menu item failed" << zooms[i]);
        QTest::qWait(50);
        const auto rect = viewport->contentRect();
        const double scale = i ? (1 << (i - 1)) / 2.0 : qMin(viewport->width() / 320., viewport->height() / 180.);
        CHECK(!rect.isEmpty() && qAbs(rect.width() - 320. * scale) < .01 && qAbs(rect.height() - 180. * scale) < .01 &&
              qAbs(rect.center().x() - viewport->width() / 2.) < .01 && qAbs(rect.center().y() - viewport->height() / 2.) < .01,
              "contentRect geometry mismatch" << zooms[i] << rect);
    }
    grid->selectSingle(0);
    video->seekToTimeMs(1000);
    auto *pos = control(window, "visualPositionButton");
    CHECK(pos && QMetaObject::invokeMethod(pos, "clicked"), "cannot choose Pos tool");
    const QPoint start = overlay->mapToScene(viewport->contentRect().center()).toPoint();
    const QPoint end = start + QPoint(24, 16);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, end, 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
    CHECK(text(grid, 0).contains(QStringLiteral("\\pos(172,98)")), "200% drag did not map 24x16 screen pixels to 12x8 script units" << text(grid, 0));
    CHECK(image(window, "workspace-zoom200.png"), "cannot capture zoomed workspace");
    undo->undo();
    CHECK(!undo->canUndo() && text(grid, 0) == tagged, "visual drag undo failed");

    CHECK(trigger(menu(window, "Video"), "Show Video Details"), "Video Details command missing");
    auto *details = window->findChild<QObject *>(QStringLiteral("videoDetailsDialog"));
    CHECK(details && details->property("visible").toBool() && image(window, "video-details.png"), "Video Details did not render");
    QMetaObject::invokeMethod(details, "close");
    CHECK(trigger(menu(window, "Help"), "Log Window"), "Help Log Window command missing");
    auto *log = qobject_cast<QQuickWindow *>(object(window->property("logWindow")));
    CHECK(log && log->isVisible() && log->modality() == Qt::NonModal, "log window missing/modal");
    QTest::qWait(400);
    auto *logText = control(log, "logText");
    auto *copy = control(log, "logCopyButton");
    auto *clear = control(log, "logClearButton");
    CHECK(logText && logText->property("text").toString().contains(QLatin1String("qt/config")), "real core log missing");
    CHECK(copy && QMetaObject::invokeMethod(copy, "clicked") && QGuiApplication::clipboard()->text() == logText->property("text").toString(), "log Copy failed");
    CHECK(image(log, "log-window.png"), "cannot capture Log Window");
    CHECK(clear && QMetaObject::invokeMethod(clear, "clicked") && logText->property("text").toString().isEmpty(), "log Clear failed");
    log->close();
    audio->closeAudio();
    video->closeVideo();
    auto *editor = vm<EditBoxViewModel>(engine, "editBoxVM");
    auto *props = vm<PropertiesViewModel>(engine, "propertiesVM");
    CHECK(editor && props, "editor or script properties VM missing");
    const QDir assets(fixture.path());
    const auto set = [prefs](const char *key, const QVariant &value) {
        return prefs->setOption(QString::fromLatin1(key), value);
    };

    // Save-on-change and timed autosave both preserve the original SRT and the ASS-only edit.
    const QByteArray originalSrt("1\r\n00:00:00,000 --> 00:00:03,000\r\nOriginal SRT\r\n\r\n");
    const QString edited = QStringLiteral("{\\pos(40,20)}Lossless recovery");
    const auto srt = assets.absoluteFilePath(QStringLiteral("original.srt"));
    CHECK(writeFile(srt, originalSrt), "cannot write SRT fixture");
    for (const bool onChange : {true, false}) {
        const QDir recovery(assets.absoluteFilePath(onChange ? QStringLiteral("change-recovery") : QStringLiteral("timed-recovery")));
        CHECK(set("Path/Auto/Save", recovery.absolutePath()) && set("App/Auto/Save Every Seconds", 1) &&
              set("App/Auto/Save", !onChange) && set("App/Auto/Save on Every Change", onChange),
              "cannot configure isolated autosave" << onChange);
        CHECK(document->openLocalPath(srt), "cannot open SRT fixture" << document->lastError());
        grid->selectSingle(0);
        editor->setText(edited);
        const auto recoveryFiles = [&] {
            return recovery.entryList({QStringLiteral("*.AUTOSAVE.ass")}, QDir::Files);
        };
        CHECK(QTest::qWaitFor([&] { return !recoveryFiles().isEmpty(); }, 3000), "ASS recovery not written" << onChange);
        const auto copies = recoveryFiles();
        CHECK(copies.size() == 1 && readFile(srt) == originalSrt && document->dirty() && text(grid, 0) == edited &&
              readFile(recovery.absoluteFilePath(copies.first())).contains(edited.toUtf8()),
              "autosave changed original SRT, marked it clean, or lost the override" << onChange << copies);
    }
    CHECK(set("App/Auto/Save", false) && set("App/Auto/Save on Every Change", false), "cannot disable autosave after regression");

    // Test with real media so neither a dummy sizing fallback nor a temporary preview copy can pass.
    const auto av1 = assets.absoluteFilePath(QStringLiteral("first-av.avi"));
    const auto av2 = assets.absoluteFilePath(QStringLiteral("second-av.avi"));
    const auto noRes = assets.absoluteFilePath(QStringLiteral("automatic-resolution.ass"));
    const auto savedRes = assets.absoluteFilePath(QStringLiteral("resolution-saved.ass"));
    CHECK(avFixture(av1, 32, 18, 8, '\x01') && avFixture(av2, 64, 36, 12, '\x09') && assFixture(noRes, ""),
          "cannot generate real A/V and no-PlayRes fixtures");
    CHECK(set("Video/Open Audio", true) && set("Subtitle/Default Resolution/Auto", true), "cannot enable video defaults");
    CHECK(document->openLocalPath(noRes) && props->playResX() == 0 && props->playResY() == 0, "fixture already has PlayRes");
    CHECK(video->openLocalPath(av1) && audio->hasAudio() && audio->audioName() == QStringLiteral("first-av.avi"),
          "first video did not load its audio" << video->lastError() << audio->audioName());
    CHECK(props->playResX() == 32 && props->playResY() == 18 && document->dirty(), "first video did not commit real PlayRes");
    const int firstDuration = audio->durationMs();
    CHECK(video->openLocalPath(av2) && audio->hasAudio() && audio->audioName() == QStringLiteral("second-av.avi") &&
          audio->durationMs() > firstDuration, "replacing video kept old audio" << video->lastError() << audio->audioName());
    CHECK(props->playResX() == 32 && props->playResY() == 18 && document->saveLocalPath(savedRes) &&
          readFile(savedRes).contains("PlayResX: 32") && readFile(savedRes).contains("PlayResY: 18"),
          "second video changed script resolution or resolution was not serialized");
    CHECK(audio->openLocalPath(wav) && video->openLocalPath(av1) && audio->hasAudio() &&
          audio->audioName() == QFileInfo(wav).fileName(), "video replacement overwrote explicitly opened audio");

    // Save and reopen actual project state, without feeding the document VM test-only values.
    const auto project = assets.absoluteFilePath(QStringLiteral("project.ass"));
    const auto noUi = assets.absoluteFilePath(QStringLiteral("no-ui.ass"));
    CHECK(assFixture(project, "PlayResX: 320\nPlayResY: 180\n", tagged.toLatin1(), 100) &&
          set("App/Auto/Load Linked Files", 1) && set("App/Save UI State", true) && document->openLocalPath(project),
          "cannot initialize project round-trip fixture");
    CHECK(video->openLocalPath(av1) && audio->openLocalPath(wav), "cannot load project media");
    CHECK(trigger(view, "Full view") && trigger(zoom, "200%"), "cannot set project zoom");
    grid->selectSingle(24);
    video->seekFrame(4);
    auto *list = control(window, "subtitleGridList");
    CHECK(list != nullptr, "subtitle grid list missing");
    QQmlExpression scroll(qmlContext(list), list, QStringLiteral("forceLayout(); positionViewAtIndex(17, ListView.Beginning)"));
    scroll.evaluate();
    CHECK(!scroll.hasError() && QTest::qWaitFor([&] { return document->gridScrollPosition() == 17; }, 1000),
          "scroll not captured" << scroll.error() << document->gridScrollPosition());
    const auto scrollOffset = [list] { return list->property("contentY").toDouble() - list->property("originY").toDouble(); };
    const auto savedOffset = scrollOffset();
    CHECK(document->saveFile(), "cannot save project");
    const auto serialized = readFile(project);
    CHECK(serialized.contains("Video File: first-av.avi") && serialized.contains("Audio File: " + QFileInfo(wav).fileName().toUtf8()) &&
          serialized.contains("Video Zoom Percent: 2") && serialized.contains("Video Position: 4") &&
          serialized.contains("Active Line: 24") && serialized.contains("Scroll Position: 17"),
          "project did not serialize relative media paths and UI state");
    audio->closeAudio();
    video->closeVideo();
    CHECK(document->newDocument() && document->openLocalPath(project), "cannot reopen project");
    CHECK(QTest::qWaitFor([&] {
        return video->filePath() == av1 && audio->hasAudio() && audio->audioName() == QFileInfo(wav).fileName() &&
            video->currentFrame() == 4 && video->zoomPercent() == 200 && grid->activeRow() == 24 &&
            document->gridScrollPosition() == 17 && qAbs(scrollOffset() - savedOffset) < 1.0;
    }, 2000), "project restore lost linked media, zoom, frame, active row or displayed scroll offset"
        << video->filePath() << audio->audioName() << video->currentFrame() << video->zoomPercent()
        << grid->activeRow() << document->gridScrollPosition() << scrollOffset());
    CHECK(set("App/Save UI State", false) && document->saveLocalPath(noUi), "cannot save project with UI state disabled");
    const auto withoutState = readFile(noUi);
    CHECK(withoutState.contains("Video File: first-av.avi") && !withoutState.contains("Video Zoom Percent:") &&
          !withoutState.contains("Video Position:") && !withoutState.contains("Active Line:") &&
          !withoutState.contains("Scroll Position:"), "Save UI State off dropped media or still serialized UI state");
    CHECK(set("App/Save UI State", true) && set("App/Auto/Load Linked Files", 0), "cannot reset options for overlay test");

    // At 200%, both axes crop and script units differ from one another and from native pixels.
    const auto mismatched = assets.absoluteFilePath(QStringLiteral("mismatched-resolution.ass"));
    const QString originalPos = QStringLiteral("{\\pos(450,510)\\b1}Zebra");
    CHECK(assFixture(mismatched, "PlayResX: 960\nPlayResY: 1080\n", originalPos.toLatin1()) &&
          document->openLocalPath(mismatched) && video->openDummyVideo(640, 360, 25.0, 250),
          "cannot load mismatched-PlayRes overlay fixture");
    CHECK(trigger(view, "Full view") && trigger(zoom, "200%"), "cannot zoom mismatched fixture");
    grid->selectSingle(0);
    video->seekToTimeMs(1000);
    QTest::qWait(100);
    viewport = qobject_cast<VideoViewportItem *>(control(window, "videoViewport"));
    overlay = qobject_cast<VisualOverlayItem *>(control(window, "visualOverlay"));
    CHECK(viewport && overlay && overlay->viewport() == viewport, "mismatched overlay missing");
    const auto rect = viewport->contentRect();
    CHECK(rect.width() == 1280 && rect.height() == 720 && rect.left() < -7 && rect.top() < -7 &&
          rect.right() > viewport->width() + 7 && rect.bottom() > viewport->height() + 7,
          "200% test must crop all four full-frame handles" << rect << viewport->size());
    const auto drag = [&](QPoint from, QPoint to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, to, 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    };
    const auto center = overlay->mapToScene(rect.center()).toPoint();
    pos = control(window, "visualPositionButton");
    CHECK(pos && QMetaObject::invokeMethod(pos, "clicked"), "cannot choose Pos for mismatched PlayRes");
    const QPoint offCenter = center - QPoint(40, 20);
    drag(offCenter, offCenter + QPoint(24, 16));
    CHECK(text(grid, 0) == QStringLiteral("{\\pos(468,534)\\b1}Zebra"), "off-center Pos used wrong script scale" << text(grid, 0));
    undo->undo();
    CHECK(!undo->canUndo() && text(grid, 0) == originalPos, "mismatched-PlayRes Pos not restored by one Undo");

    auto *clip = control(window, "visualClipButton");
    CHECK(clip && QMetaObject::invokeMethod(clip, "clicked"), "cannot choose Clip for mismatched PlayRes");
    const auto clipFrom = center - QPoint(24, 16), clipTo = center + QPoint(24, 16);
    const auto scriptPoint = [&](QPoint scene) {
        const auto local = overlay->mapFromScene(scene) - rect.topLeft();
        return QPoint(qRound(local.x() * 960. / rect.width()), qRound(local.y() * 1080. / rect.height()));
    };
    const auto topLeft = scriptPoint(clipFrom), bottomRight = scriptPoint(clipTo);
    const auto clipTag = [](QPoint a, QPoint b) {
        return QStringLiteral("\\clip(%1,%2,%3,%4)").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y());
    };
    drag(clipFrom, clipTo);
    CHECK(text(grid, 0).contains(clipTag(topLeft, bottomRight)), "fresh Clip translated the full-frame placeholder or used wrong coordinates" << text(grid, 0));
    drag(clipTo, clipTo + QPoint(24, 16));
    CHECK(text(grid, 0).contains(clipTag(topLeft, bottomRight + QPoint(18, 24))),
          "visible Clip corner did not resize with independent X/Y scale" << text(grid, 0));
    undo->undo();
    CHECK(text(grid, 0).contains(clipTag(topLeft, bottomRight)), "Clip resize not restored by one Undo");
    undo->undo();
    CHECK(!undo->canUndo() && text(grid, 0) == originalPos, "Clip creation not restored by one Undo");

    qInfo() << "Stage 15B integration passed";
    return 0;
}
