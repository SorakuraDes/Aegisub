#pragma once

#include <QObject>
#include <QString>

class AssDialogue;
class SubtitleGridModel;

/// View model for the single-line edit box at the bottom of the window.
///
/// Reads/writes the AssDialogue selected in SubtitleGridModel and tells the
/// grid model to emit dataChanged for the edited row (no undo/commit yet).
class EditBoxViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString startTime READ startTime WRITE setStartTime NOTIFY startTimeChanged)
    Q_PROPERTY(QString endTime READ endTime WRITE setEndTime NOTIFY endTimeChanged)

public:
    explicit EditBoxViewModel(SubtitleGridModel *model, QObject *parent = nullptr);

    QString text() const;
    void setText(const QString &value);

    QString startTime() const;
    void setStartTime(const QString &value);

    QString endTime() const;
    void setEndTime(const QString &value);

signals:
    void textChanged();
    void startTimeChanged();
    void endTimeChanged();
    /// Emitted whenever the underlying dialogue line was actually modified.
    void edited();

private:
    AssDialogue *active() const;
    void reload();

    SubtitleGridModel *model_;
};
