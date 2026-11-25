#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QQmlEngine;

/// Gettext catalog lookup and shared App/Language configuration for Qt.
class TranslationService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QString preference READ preference NOTIFY preferenceChanged)
    Q_PROPERTY(bool forced READ forced CONSTANT)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit TranslationService(QObject *parent = nullptr);

    void setQmlEngine(QQmlEngine *engine);
    QString language() const { return language_; }
    QString preference() const { return preference_; }
    bool forced() const { return !forced_.isEmpty(); }
    QVariantList availableLanguages() const;
    QString error() const { return error_; }

    /// Looks up msgid exactly, then as a plain label for a catalog mnemonic.
    /// Returns msgid when neither lookup finds a translation.
    Q_INVOKABLE QString t(const QString &msgid) const;
    /// Empty code follows the system UI language. Failure leaves the active
    /// catalog and the shared option unchanged.
    Q_INVOKABLE bool setLanguage(const QString &code);
    Q_INVOKABLE void clearError();

signals:
    void languageChanged();
    void preferenceChanged();
    void errorChanged();

private:
    using Messages = QHash<QByteArray, QString>;

    struct Catalog {
        Messages exact;
        Messages withoutMnemonics;
        bool operator!=(const Catalog &other) const {
            return exact != other.exact || withoutMnemonics != other.withoutMnemonics;
        }
    };

    QStringList candidates(const QString &requested) const;
    QStringList systemCandidates() const;
    bool load(const QString &code, Catalog &catalog) const;
    void activate(const QStringList &codes);
    bool fail(const QString &error);

    QPointer<QQmlEngine> engine_;
    QHash<QString, QString> catalog_paths_;
    QStringList catalog_order_;
    Catalog catalog_;
    QString language_;
    QString preference_;
    QString forced_;
    QString error_;
};
