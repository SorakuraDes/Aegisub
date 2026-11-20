#pragma once

#include <QObject>
#include <QString>

class TranslationService;

/// Read-only view model for the About dialog, mirroring the data the original
/// wxWidgets dialog_about.cpp shows (src/dialog_about.cpp).
class AboutViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString versionInfo READ versionInfo CONSTANT)
    Q_PROPERTY(QString copyright READ copyright CONSTANT)
    Q_PROPERTY(QString translatorsText READ translatorsText CONSTANT)

public:
    explicit AboutViewModel(TranslationService *i18n, QObject *parent = nullptr);

    QString appName() const;
    QString versionInfo() const;
    QString copyright() const;
    QString translatorsText() const;

private:
    QString app_name_;
    QString version_info_;
    QString copyright_;
    QString translators_text_;
};
