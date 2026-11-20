#pragma once

#include <QObject>
#include <QString>

/// View model for the Preferences dialog.
///
/// Values are read from / written to the original libaegisub option store
/// (src/options.h, OPT_GET/OPT_SET), i.e. the very same config.json the
/// wxWidgets frontend uses.
class PreferencesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY autoSaveChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath CONSTANT)

public:
    explicit PreferencesViewModel(QObject *parent = nullptr);

    bool autoSave() const;
    void setAutoSave(bool value);

    /// config.json that changes are persisted to.
    QString configFilePath() const;

signals:
    void autoSaveChanged();
};
