// Before Qt headers: libaegisub's signal header uses `slots` as a member name.
#include "../../options.h"
#include <libaegisub/path.h>

#include "translation_service.h"
#include "../aegisub_core.h"
#include "acconf.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtEndian>

#include <exception>
#include <utility>

namespace {
constexpr auto languageOption = "App/Language";
const auto english = QStringLiteral("en_US");
const auto catalogSuffix = QStringLiteral("/LC_MESSAGES/aegisub.mo");

QStringList translationRoots() {
    const QString fromEnv = qEnvironmentVariable("AEGISUB_PO_DIR");
    if (!fromEnv.isEmpty())
        return {fromEnv};

    const QDir app(QCoreApplication::applicationDirPath());
    QStringList roots;
    // Meson build tree, then portable and installed locations.
    for (const auto *relative : {"../../po", "../po", "po", "locale", "../share/locale"})
        roots << QDir::cleanPath(app.filePath(QString::fromLatin1(relative)));
    if (config::path)
        roots << QString::fromStdWString(config::path->Decode("?data/locale/").wstring());
#ifdef P_LOCALE
    roots << QStringLiteral(P_LOCALE);
#endif
    roots.removeDuplicates();
    return roots;
}

QLocale catalogLocale(QString code) {
    if (code.endsWith(QLatin1String("@latin")))
        code = code.section(QLatin1Char('@'), 0, 0).section(QLatin1Char('_'), 0, 0)
             + QStringLiteral("_Latn_") + code.section(QLatin1Char('@'), 0, 0).section(QLatin1Char('_'), 1, 1);
    return QLocale(code);
}

QString displayName(const QString &code) {
    const auto locale = catalogLocale(code);
    QString name = locale.nativeLanguageName();
    if (name.isEmpty())
        return code;
    const QString territory = locale.nativeTerritoryName();
    if (code.contains(QLatin1Char('_')) && !territory.isEmpty())
        name += QStringLiteral(" (%1)").arg(territory);
    return QStringLiteral("%1 [%2]").arg(name, code);
}
QString withoutMnemonics(QString text) {
    // Consume escaped ampersands before matching single mnemonic markers.
    static const QRegularExpression markers(QStringLiteral(R"(\(&[^&()]\)|&&|&)"));
    QString result;
    qsizetype offset = 0;
    auto matches = markers.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += QStringView(text).mid(offset, match.capturedStart() - offset);
        if (match.capturedLength() == 2)
            result += QLatin1Char('&');
        offset = match.capturedEnd();
    }
    result += QStringView(text).mid(offset);
    return result;
}
} // namespace

TranslationService::TranslationService(QObject *parent) : QObject(parent) {
    const QRegularExpression localeCode(QStringLiteral("^[A-Za-z]{2,3}(?:_[A-Za-z0-9]+)*(?:@[A-Za-z0-9_-]+)?$"));
    for (const auto &root : translationRoots()) {
        const QDir dir(root);
        for (const auto &code : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (code == english || catalog_paths_.contains(code) || !localeCode.match(code).hasMatch())
                continue;
            const QFileInfo catalog(dir.filePath(code + catalogSuffix));
            if (catalog.isFile() && catalog.isReadable())
                catalog_paths_.insert(code, catalog.absoluteFilePath());
        }
    }
    catalog_order_ = catalog_paths_.keys();
    catalog_order_.sort(Qt::CaseInsensitive);
    catalog_order_.prepend(english);

    if (config::opt) {
        try {
            preference_ = QString::fromStdString(OPT_GET(languageOption)->GetString());
        } catch (const agi::Exception &e) {
            qWarning("Could not read App/Language: %s", e.GetMessage().c_str());
        }
    }
    forced_ = qEnvironmentVariable("AEGISUB_QT_LANGUAGE").trimmed();
    if (forced())
        activate(candidates(forced_) << english);
    else
        activate(candidates(preference_) + systemCandidates());
}

void TranslationService::setQmlEngine(QQmlEngine *engine) {
    engine_ = engine;
}

QVariantList TranslationService::availableLanguages() const {
    QVariantList list;
    for (const auto &code : catalog_order_)
        list.append(QVariantMap{{QStringLiteral("code"), code}, {QStringLiteral("name"), displayName(code)}});
    return list;
}

QStringList TranslationService::candidates(const QString &requested) const {
    if (requested.isEmpty())
        return {};
    // Accept POSIX/BCP47 spellings while retaining gettext's exact directory id.
    QString code = requested;
    code.replace(QLatin1Char('-'), QLatin1Char('_'));
    const int dot = code.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        const int modifier = code.indexOf(QLatin1Char('@'), dot);
        code.remove(dot, modifier < 0 ? code.size() - dot : modifier - dot);
    }
    const QLocale locale = catalogLocale(code);
    if (locale.language() == QLocale::English)
        return {english};
    QStringList codes{code};
    if (code.startsWith(QLatin1String("sr_Latn"), Qt::CaseInsensitive))
        codes << QStringLiteral("sr_RS@latin");
    if (locale.language() != QLocale::C)
        codes << locale.name() << locale.name().section(QLatin1Char('_'), 0, 0);
    codes.removeDuplicates();
    return codes;
}

QStringList TranslationService::systemCandidates() const {
    QStringList codes;
    for (const auto &language : QLocale::system().uiLanguages())
        codes += candidates(language);
    codes << english;
    codes.removeDuplicates();
    return codes;
}

bool TranslationService::load(const QString &code, Catalog &catalog) const {
    if (code == english) {
        catalog = {};
        return true;
    }
    const auto path = catalog_paths_.constFind(code);
    if (path == catalog_paths_.cend())
        return false;
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    if (data.size() < 28 || file.error() != QFileDevice::NoError)
        return false;

    // GNU MO revision 0/1 originals and translations tables. Treat all
    // offsets as untrusted and check bounds before taking a pointer or slice.
    constexpr quint32 magic = 0x950412de;
    const bool little = qFromLittleEndian<quint32>(data.constData()) == magic;
    if (!little && qFromBigEndian<quint32>(data.constData()) != magic)
        return false;
    auto u32 = [&](quint64 offset) {
        const auto *p = data.constData() + offset;
        return little ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
    };
    const quint64 size = data.size();
    const quint64 count = u32(8);
    const quint64 originals = u32(12);
    const quint64 translations = u32(16);
    if (originals + count * 8 > size || translations + count * 8 > size)
        return false;

    Catalog next;
    for (quint64 i = 0; i < count; ++i) {
        const quint64 olen = u32(originals + i * 8);
        const quint64 ooff = u32(originals + i * 8 + 4);
        const quint64 tlen = u32(translations + i * 8);
        const quint64 toff = u32(translations + i * 8 + 4);
        if (ooff + olen > size || toff + tlen > size)
            return false;
        if (olen && tlen) {
            const auto id = data.mid(ooff, olen);
            if (id.contains('\0'))
                continue;
            const auto value = withoutMnemonics(QString::fromUtf8(data.constData() + toff, tlen));
            next.exact.insert(id, value);
            if (id.contains('&')) {
                const auto plain = withoutMnemonics(QString::fromUtf8(id)).toUtf8();
                // In case of collisions keep the first entry in catalog order.
                if (!next.withoutMnemonics.contains(plain))
                    next.withoutMnemonics.insert(plain, value);
            }
        }
    }
    // The wx catalog has no plain Timing/View msgids, and its mnemonic
    // versions use different wording. It leaves Stripped Text untranslated.
    if (code == QStringLiteral("zh_CN")) {
        const auto addQtLabel = [&next](const QByteArray &id, const QString &value) {
            if (!next.exact.contains(id))
                next.exact.insert(id, value);
        };
        addQtLabel("Timing", QStringLiteral("时间"));
        addQtLabel("View", QStringLiteral("视图"));
        addQtLabel("Stripped Text", QStringLiteral("去除标签的文本"));
        addQtLabel("OK", QStringLiteral("确定"));
        addQtLabel("Apply", QStringLiteral("应用"));
        addQtLabel("Matched %1 line(s); selected %2 line(s)", QStringLiteral("匹配 %1 行；已选中 %2 行"));
        addQtLabel("The document or selection has changed.", QStringLiteral("文档或选区已更改。"));
        addQtLabel("Attach Fonts", QStringLiteral("附加字体"));
        addQtLabel("Encoded size", QStringLiteral("编码后大小"));
        addQtLabel("No attachments", QStringLiteral("无附件"));
        addQtLabel("bytes", QStringLiteral("字节"));
        addQtLabel("Extract Attachment", QStringLiteral("提取附件"));
        addQtLabel("Extract Attachments to Folder", QStringLiteral("提取附件到文件夹"));
        addQtLabel("Copy Log", QStringLiteral("复制日志"));
        addQtLabel("Collecting fonts...", QStringLiteral("正在收集字体…"));
        addQtLabel("Font collection failed", QStringLiteral("字体收集失败"));
        addQtLabel("Font files found: %1; missing: %2", QStringLiteral("找到 %1 个字体文件；缺失 %2 项"));
        addQtLabel("Zip Archives (*.zip)", QStringLiteral("ZIP 压缩文件 (*.zip)"));
    }
    catalog = std::move(next);
    return true;
}

void TranslationService::activate(const QStringList &codes) {
    for (const auto &code : codes) {
        if (!load(code, catalog_))
            continue;
        language_ = code;
        return;
    }
}

QString TranslationService::t(const QString &msgid) const {
    // Register every use in a live QML binding, including nested JS calls,
    // with the engine's translation dependency. C++ callers still use t().
    if (engine_)
        engine_->markCurrentFunctionAsTranslationBinding();
    const auto key = msgid.toUtf8();
    if (const auto exact = catalog_.exact.constFind(key); exact != catalog_.exact.cend())
        return *exact;
    if (const auto plain = catalog_.withoutMnemonics.constFind(key); plain != catalog_.withoutMnemonics.cend())
        return *plain;
    return msgid;
}

void TranslationService::clearError() {
    if (error_.isEmpty())
        return;
    error_.clear();
    emit errorChanged();
}

bool TranslationService::fail(const QString &error) {
    if (error_ != error) {
        error_ = error;
        emit errorChanged();
    }
    return false;
}

bool TranslationService::setLanguage(const QString &code) {
    if (forced())
        return fail(t(QStringLiteral("Language is set by AEGISUB_QT_LANGUAGE.")));

    QString nextLanguage = code;
    Catalog nextCatalog;
    if (code.isEmpty()) {
        for (const auto &candidate : systemCandidates()) {
            if (load(candidate, nextCatalog)) {
                nextLanguage = candidate;
                break;
            }
        }
    } else if (!load(code, nextCatalog)) {
        return fail(t(QStringLiteral("Could not load the selected language catalog.")));
    }

    if (!config::opt)
        return fail(t(QStringLiteral("The shared configuration is unavailable.")));
    QString saveError;
    try {
        auto *option = OPT_SET(languageOption);
        const auto previous = option->GetString();
        if (const auto requested = code.toStdString(); requested != previous) {
            option->SetString(requested);
            if (!aegisub_qt::FlushOptions(&saveError)) {
                option->SetString(previous);
                return fail(saveError.isEmpty() ? t(QStringLiteral("Could not write the configuration file.")) : saveError);
            }
        }
    } catch (const agi::Exception &e) {
        return fail(QString::fromStdString(e.GetMessage()));
    } catch (const std::exception &e) {
        return fail(QString::fromUtf8(e.what()));
    } catch (...) {
        return fail(t(QStringLiteral("Could not write the configuration file.")));
    }

    const bool preferenceChanged = preference_ != code;
    const bool languageChanged = language_ != nextLanguage || catalog_ != nextCatalog;
    preference_ = code;
    language_ = nextLanguage;
    catalog_ = std::move(nextCatalog);
    clearError();
    if (preferenceChanged)
        emit this->preferenceChanged();
    if (languageChanged)
        emit this->languageChanged();
    return true;
}
