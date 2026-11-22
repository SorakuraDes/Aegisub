#pragma once

#include <QObject>
#include <QString>

class AssFile;
class SubtitleGridModel;
class VideoViewModel;

/// View model for the non-modal Shift Times window (Subtitle -> Shift Times).
///
/// Reimplements src/dialog_shift_times.cpp's Process()/Shift() semantics
/// against the Qt frontend's data model: the Start/End times of the affected
/// dialogue lines are shifted either by an agi::Time amount or by a frame count
/// (mapped through the open video), and the whole batch lands in a single
/// AssFile::Commit, so one Ctrl+Z reverts every shifted line.
class ShiftTimesViewModel final : public QObject {
    Q_OBJECT

    /// Shift by a frame count instead of by time. Requires an open video.
    Q_PROPERTY(bool byFrames READ byFrames WRITE setByFrames NOTIFY byFramesChanged)
    /// A video is open, so the frame mode is usable.
    Q_PROPERTY(bool framesAvailable READ framesAvailable NOTIFY framesAvailableChanged)
    /// "h:mm:ss.cs" amount to shift by when byFrames is false.
    Q_PROPERTY(QString shiftTime READ shiftTime WRITE setShiftTime NOTIFY shiftTimeChanged)
    /// Frame count to shift by when byFrames is true.
    Q_PROPERTY(int shiftFrames READ shiftFrames WRITE setShiftFrames NOTIFY shiftFramesChanged)
    /// false = forward (subtitles appear later), true = backward (earlier).
    Q_PROPERTY(bool backward READ backward WRITE setBackward NOTIFY backwardChanged)
    /// 0 = all rows, 1 = selected rows, 2 = first selected row through the end.
    Q_PROPERTY(int affectScope READ affectScope WRITE setAffectScope NOTIFY affectScopeChanged)
    /// 0 = start and end, 1 = start only, 2 = end only.
    Q_PROPERTY(int timeTarget READ timeTarget WRITE setTimeTarget NOTIFY timeTargetChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    ShiftTimesViewModel(AssFile *file, SubtitleGridModel *gridModel, VideoViewModel *videoVM,
                        QObject *parent = nullptr);

    bool byFrames() const;
    void setByFrames(bool value);

    bool framesAvailable() const;

    QString shiftTime() const;
    void setShiftTime(const QString &value);

    int shiftFrames() const;
    void setShiftFrames(int value);

    bool backward() const;
    void setBackward(bool value);

    int affectScope() const;
    void setAffectScope(int value);

    int timeTarget() const;
    void setTimeTarget(int value);

    QString statusMessage() const;

    /// Shift the Start/End times of the targeted lines and commit them once.
    /// The grid / edit box / video / audio refresh through the normal
    /// AssFile::Commit bus; nothing is notified by hand.
    /// @return number of lines whose Start/End actually changed
    Q_INVOKABLE int applyShift();

signals:
    void byFramesChanged();
    void framesAvailableChanged();
    void shiftTimeChanged();
    void shiftFramesChanged();
    void backwardChanged();
    void affectScopeChanged();
    void timeTargetChanged();
    void statusMessageChanged();

private:
    /// Emit framesAvailableChanged and fall back to time mode if the video closed.
    void onVideoAvailabilityChanged();
    void setStatus(const QString &message);
    /// Parse shiftTime_ into milliseconds; false when it is empty or not a time.
    bool parseShiftTime(int *ms) const;

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    VideoViewModel *video_vm_;

    bool by_frames_ = false;
    QString shift_time_ = QStringLiteral("0:00:00.00");
    int shift_frames_ = 0;
    bool backward_ = false;
    int affect_scope_ = 0;
    int time_target_ = 0;
    QString status_;
};
