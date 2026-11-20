#pragma once

#include <QObject>
#include <QString>

class AssFile;
class TranslationService;

/// View model for the script properties dialog.
///
/// Edits the real in-memory AssFile ([Script Info] entries) that the wxWidgets
/// frontend would edit through dialog_properties.cpp.
class PropertiesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString scriptTitle READ scriptTitle WRITE setScriptTitle NOTIFY scriptTitleChanged)
    Q_PROPERTY(int playResX READ playResX WRITE setPlayResX NOTIFY playResXChanged)
    Q_PROPERTY(int playResY READ playResY WRITE setPlayResY NOTIFY playResYChanged)

public:
    explicit PropertiesViewModel(AssFile *file, TranslationService *i18n, QObject *parent = nullptr);

    QString scriptTitle() const;
    void setScriptTitle(const QString &title);

    int playResX() const;
    void setPlayResX(int value);

    int playResY() const;
    void setPlayResY(int value);

    /// Serialize the current AssFile to text and print it to the console.
    Q_INVOKABLE void dumpAss();

signals:
    void scriptTitleChanged();
    void playResXChanged();
    void playResYChanged();
    /// Emitted whenever an AssFile script-info value was actually modified.
    void edited();

private:
    AssFile *file_;
    TranslationService *i18n_;
};
