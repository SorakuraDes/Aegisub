#pragma once

// agi::signal has members named `slots`; keep this before any Qt header.
#include <libaegisub/signal.h>

#include <QObject>
#include <QString>
#include <QTimer>

class AssFile;
class TranslationService;
class UndoManager;

/// View model for the script properties dialog.
///
/// Edits the real in-memory AssFile ([Script Info] entries) that the wxWidgets
/// frontend would edit through dialog_properties.cpp, and routes every write
/// through AssFile::Commit so it lands on the shared undo stack.
class PropertiesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString scriptTitle READ scriptTitle WRITE setScriptTitle NOTIFY scriptTitleChanged)
    Q_PROPERTY(int playResX READ playResX WRITE setPlayResX NOTIFY playResXChanged)
    Q_PROPERTY(int playResY READ playResY WRITE setPlayResY NOTIFY playResYChanged)

public:
    PropertiesViewModel(AssFile *file, TranslationService *i18n, UndoManager *undoManager,
                        QObject *parent = nullptr);

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

private:
    void commitScriptInfo(const QString &description);
    /// UndoManager::restored slot: the [Script Info] section may have changed.
    void onRestored();

    AssFile *file_;
    TranslationService *i18n_;
    UndoManager *undo_manager_;

    /// Coalesces consecutive keystrokes of the same field into one undo step.
    QTimer coalesce_timer_;
    int commit_id_ = -1;
};
