#pragma once

#include <libaegisub/signal.h>
#include <QObject>
#include <QVariantMap>
#include <vector>

class PreferencesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap optionValues READ optionValues NOTIFY optionsChanged)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY optionsChanged)
    Q_PROPERTY(bool syntaxHighlighting READ syntaxHighlighting NOTIFY optionsChanged)
    Q_PROPERTY(int editFontSize READ editFontSize NOTIFY optionsChanged)
    Q_PROPERTY(int gridFontSize READ gridFontSize NOTIFY optionsChanged)
    Q_PROPERTY(QString hideOverridesChar READ hideOverridesChar NOTIFY optionsChanged)
    Q_PROPERTY(int characterLimit READ characterLimit NOTIFY optionsChanged)
    Q_PROPERTY(int preferencePage READ preferencePage WRITE setPreferencePage NOTIFY optionsChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath CONSTANT)
    Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY persistenceErrorChanged)

public:
    explicit PreferencesViewModel(QObject *parent = nullptr);
    QVariantMap optionValues() const;
    Q_INVOKABLE bool setOption(const QString &key, const QVariant &value);
    bool autoSave() const;
    void setAutoSave(bool value);
    bool syntaxHighlighting() const;
    int editFontSize() const;
    int gridFontSize() const;
    QString hideOverridesChar() const;
    int characterLimit() const;
    int preferencePage() const;
    void setPreferencePage(int value);
    QString configFilePath() const;
    QString persistenceError() const;
    Q_INVOKABLE bool retrySave();

signals:
    void optionsChanged();
    void persistenceErrorChanged();

private:
    bool flushChanges();
    QVariant option(const char *key) const;
    QVariantMap values_;
    QString persistence_error_;
    std::vector<agi::signal::Connection> subscriptions_;
};
