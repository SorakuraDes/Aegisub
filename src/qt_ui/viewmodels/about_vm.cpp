#include "about_vm.h"
#include "translation_service.h"

// Same version API the wxWidgets About dialog uses (src/dialog_about.cpp).
#include "../../version.h"

// libaegisub core: agi::format is used to compose the strings.
#include <libaegisub/format.h>

#include <QStringList>

#include <string>

AboutViewModel::AboutViewModel(TranslationService *i18n, QObject *parent)
: QObject(parent)
, app_name_(QStringLiteral("Aegisub"))
, version_info_(QString::fromStdString(
      agi::format("Aegisub %s", GetAegisubShortVersionString())))
, copyright_(QString::fromUtf8(
      "Copyright \u00a9 2005-2019 Rodrigo Braz Monteiro, Niels Martin Hansen, Thomas Goyne et al."))
{
#ifdef BUILD_CREDIT
    version_info_ += QString::fromStdString(
        agi::format("\nBuilt by %s on %s.", GetAegisubBuildCredit(), GetAegisubBuildTime()));
#endif

    // dialog_about.cpp: translatorCredit = _("Translated into LANGUAGE by PERSON\n"),
    // dropped when untranslated, plus the translated "See the help file" line.
    const QString credit_msgid = QStringLiteral("Translated into LANGUAGE by PERSON\n");
    QString credit = i18n ? i18n->t(credit_msgid) : QString();
    if (credit == credit_msgid)
        credit.clear();

    QString see_help = i18n ? i18n->t(QStringLiteral("\nSee the help file for full credits.\n"))
                            : QString();
    if (see_help.trimmed().isEmpty())
        see_help = QStringLiteral("See the help file for full credits.");

    QStringList lines;
    if (!credit.trimmed().isEmpty())
        lines << credit.trimmed();
    lines << see_help.trimmed();
    translators_text_ = lines.join('\n');
}

QString AboutViewModel::appName() const {
    return app_name_;
}

QString AboutViewModel::versionInfo() const {
    return version_info_;
}

QString AboutViewModel::copyright() const {
    return copyright_;
}

QString AboutViewModel::translatorsText() const {
    return translators_text_;
}
