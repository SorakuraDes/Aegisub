#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/// Application-wide translation service.
///
/// The wxWidgets build resolves translated strings through wxTranslations
/// (gettext .mo catalogs built from po/). This service provides the same data
/// to QML: it locates build/po/<lang>/LC_MESSAGES/aegisub.mo for the current
/// locale and exposes a gettext-style lookup, `i18n.t("msgid")` for QML.
class TranslationService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language CONSTANT)

public:
    explicit TranslationService(QObject *parent = nullptr);

    /// Locale used for lookups, e.g. "zh_CN".
    QString language() const;

    /// Gettext-style lookup: returns the translated string, or msgid itself
    /// when the catalog has no translation for it.
    Q_INVOKABLE QString t(const QString &msgid) const;

private:
    QString translate(const QByteArray &msgid) const;

    QString base_dir_;
    QStringList catalog_locales_;
};
