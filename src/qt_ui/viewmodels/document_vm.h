#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class AssFile;
class SubtitleGridModel;

/// Owns the open document: current file path, dirty state, and the actual
/// AssFile I/O through the original AssSubtitleFormat reader/writer.
class DocumentViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    DocumentViewModel(AssFile *file, SubtitleGridModel *gridModel, QObject *parent = nullptr);

    QString filePath() const;
    QString displayName() const;
    bool dirty() const;
    QString lastError() const;

    Q_INVOKABLE bool newDocument();
    Q_INVOKABLE bool openFile(const QUrl &url);
    Q_INVOKABLE bool openLocalPath(const QString &path);
    Q_INVOKABLE bool saveFile();
    Q_INVOKABLE bool saveFileAs(const QUrl &url);
    Q_INVOKABLE bool saveLocalPath(const QString &path);
    Q_INVOKABLE void markDirty();
    Q_INVOKABLE void clearError();

signals:
    void filePathChanged();
    void displayNameChanged();
    void dirtyChanged();
    void lastErrorChanged();

private:
    bool loadFromPath(const QString &path);
    bool saveToPath(const QString &path);
    void setFilePath(const QString &path);
    void setDirty(bool dirty);
    void setError(const QString &message);
    void clearErrorIfNeeded();

    AssFile *file_;
    SubtitleGridModel *gridModel_;
    QString file_path_;
    QString last_error_;
    bool dirty_ = false;
};
