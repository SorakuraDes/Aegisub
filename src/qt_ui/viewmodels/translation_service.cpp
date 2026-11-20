#include "translation_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>

#include <cstring>

namespace {

/// Minimal gettext .mo reader: returns the msgstr for an exact msgid match.
bool readMoString(const QString &mo_path, const QByteArray &msgid, QByteArray *out) {
    QFile file(mo_path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    auto u32 = [&data](int off) -> quint32 {
        const uchar *p = reinterpret_cast<const uchar *>(data.constData() + off);
        return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    };

    if (data.size() < 28 || u32(0) != 0x950412de)
        return false;

    const quint32 count = u32(8);
    const quint32 originals = u32(12);
    const quint32 translations = u32(16);

    for (quint32 i = 0; i < count; ++i) {
        const quint32 olen = u32(int(originals + i * 8));
        const quint32 ooff = u32(int(originals + i * 8 + 4));
        if (olen != quint32(msgid.size()) || quint32(data.size()) < ooff + olen)
            continue;
        if (std::memcmp(data.constData() + ooff, msgid.constData(), olen) != 0)
            continue;

        const quint32 tlen = u32(int(translations + i * 8));
        const quint32 toff = u32(int(translations + i * 8 + 4));
        if (tlen == 0 || quint32(data.size()) < toff + tlen)
            return false; // untranslated message

        *out = data.mid(int(toff), int(tlen));
        return true;
    }
    return false;
}

QString translationBaseDir() {
    const QByteArray from_env = qgetenv("AEGISUB_PO_DIR");
    if (!from_env.isEmpty())
        return QString::fromLocal8Bit(from_env);

    // Running from build/src/qt_ui/aegisub-qt.exe -> build/po
    const QDir app(QCoreApplication::applicationDirPath());
    for (const char *rel : { "../../po", "../po", "po" }) {
        const QDir candidate(app.filePath(QString::fromLatin1(rel)));
        if (candidate.exists())
            return candidate.absolutePath();
    }
    return QString();
}

} // namespace

TranslationService::TranslationService(QObject *parent)
: QObject(parent)
, base_dir_(translationBaseDir())
{
    // AEGISUB_QT_LANGUAGE is a developer hook to force a catalog for testing;
    // otherwise follow the system locale like the wxWidgets build does.
    const QString forced = qEnvironmentVariable("AEGISUB_QT_LANGUAGE");
    const QLocale locale = forced.isEmpty() ? QLocale::system() : QLocale(forced);

    catalog_locales_ << locale.name();
    if (const QString lang = locale.name().section('_', 0, 0); lang != locale.name())
        catalog_locales_ << lang;
}

QString TranslationService::language() const {
    return catalog_locales_.value(0);
}

QString TranslationService::translate(const QByteArray &msgid) const {
    if (base_dir_.isEmpty())
        return QString();

    for (const QString &lang : catalog_locales_) {
        const QString mo = base_dir_ + "/" + lang + "/LC_MESSAGES/aegisub.mo";
        QByteArray translated;
        if (readMoString(mo, msgid, &translated))
            return QString::fromUtf8(translated);
    }
    return QString();
}

QString TranslationService::t(const QString &msgid) const {
    const QString translated = translate(msgid.toUtf8());
    return translated.isEmpty() ? msgid : translated;
}
