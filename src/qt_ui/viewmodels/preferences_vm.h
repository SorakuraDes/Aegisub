#pragma once

#include <libaegisub/signal.h>
#include <QObject>
#include <QColor>
#include <QVariantMap>
#include <vector>

class PreferencesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap optionValues READ optionValues NOTIFY optionsChanged)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY optionsChanged)
    Q_PROPERTY(bool syntaxHighlighting READ syntaxHighlighting NOTIFY optionsChanged)
    Q_PROPERTY(int editFontSize READ editFontSize NOTIFY optionsChanged)
    Q_PROPERTY(int gridFontSize READ gridFontSize NOTIFY optionsChanged)
    Q_PROPERTY(QString editFontFace READ editFontFace NOTIFY optionsChanged)
    Q_PROPERTY(QString gridFontFace READ gridFontFace NOTIFY optionsChanged)
    Q_PROPERTY(int cpsWarningThreshold READ cpsWarningThreshold NOTIFY optionsChanged)
    Q_PROPERTY(int cpsErrorThreshold READ cpsErrorThreshold NOTIFY optionsChanged)
    Q_PROPERTY(bool ignoreWhitespace READ ignoreWhitespace NOTIFY optionsChanged)
    Q_PROPERTY(bool ignorePunctuation READ ignorePunctuation NOTIFY optionsChanged)
    Q_PROPERTY(QVariantMap audioColors READ audioColors NOTIFY colorsChanged)
    Q_PROPERTY(QVariantMap syntaxColors READ syntaxColors NOTIFY colorsChanged)
    Q_PROPERTY(QVariantMap gridColors READ gridColors NOTIFY colorsChanged)
    Q_PROPERTY(QVariantMap visualColors READ visualColors NOTIFY colorsChanged)
    Q_PROPERTY(quint32 colorRevision READ colorRevision NOTIFY colorsChanged)
    Q_PROPERTY(QString hideOverridesChar READ hideOverridesChar NOTIFY optionsChanged)
    Q_PROPERTY(int characterLimit READ characterLimit NOTIFY optionsChanged)
    Q_PROPERTY(int preferencePage READ preferencePage WRITE setPreferencePage NOTIFY optionsChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath CONSTANT)
    Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY persistenceErrorChanged)

public:
    explicit PreferencesViewModel(QObject *parent = nullptr);
    QVariantMap optionValues() const;
    Q_INVOKABLE QVariant option(const QString &key) const;
    Q_INVOKABLE bool setOption(const QString &key, const QVariant &value);
    bool autoSave() const;
    void setAutoSave(bool value);
    bool syntaxHighlighting() const;
    int editFontSize() const;
    int gridFontSize() const;
    QString editFontFace() const;
    QString gridFontFace() const;
    int cpsWarningThreshold() const;
    int cpsErrorThreshold() const;
    bool ignoreWhitespace() const;
    bool ignorePunctuation() const;
    quint32 colorRevision() const { return color_revision_; }
    QVariantMap audioColors() const;
    QVariantMap syntaxColors() const;
    QVariantMap gridColors() const;
    QVariantMap visualColors() const;
    Q_INVOKABLE QColor effectiveColor(const QString &group, const QString &key, const QColor &fallback) const;
    Q_INVOKABLE bool colorOverridesEnabled(const QString &group) const;
    Q_INVOKABLE bool setColorOverridesEnabled(const QString &group, bool enabled);
    Q_INVOKABLE bool setColorOverride(const QString &group, const QString &key, const QColor &color);
    Q_INVOKABLE bool clearColorOverride(const QString &group, const QString &key);
    QString hideOverridesChar() const;
    int characterLimit() const;
    int preferencePage() const;
    void setPreferencePage(int value);
    QString configFilePath() const;
    QString persistenceError() const;
    Q_INVOKABLE bool retrySave();

signals:
    void optionsChanged();
    void colorsChanged();
    void persistenceErrorChanged();

private:
    bool flushChanges();
    QVariant option(const char *key) const;
    QVariantMap groupColors(const QString &group) const;
    bool flushColorGroups();
    void updatePersistenceError();
    void notifyColorsChanged();
    QVariantMap values_;
    QVariantMap color_groups_;
    quint32 color_revision_ = 0;
    bool color_groups_dirty_ = false;
    QString options_error_;
    QString color_groups_error_;
    QString persistence_error_;
    std::vector<agi::signal::Connection> subscriptions_;
};
