#pragma once

// agi::signal (pulled in by ass_file.h) has a member named `slots`, which Qt's
// `#define slots` would mangle. See undo_manager.h for the same pattern.
#include <libaegisub/signal.h>

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <memory>

class AssDialogue;
class AssFile;
class SubtitleGridModel;
class UndoManager;

namespace aegisub_qt {
class VideoBridge;
}

/// View model for the M2a video preview.
///
/// Wraps aegisub_qt::VideoBridge (av_qt_core): opening a real video goes through
/// ffms2 + the original FFmpegSourceVideoProvider, the dummy video uses the
/// original DummyVideoProvider, and libass renders the current AssFile on top of
/// every frame. Edits, style changes and undo/redo reload the libass track and
/// repaint the current frame immediately, so the preview is always WYSIWYG.
class VideoViewModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoNameChanged)
    /// Absolute path of the real video file (empty for the dummy video).
    Q_PROPERTY(QString filePath READ filePath NOTIFY videoNameChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY videoInfoChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(int currentTimeMs READ currentTimeMs NOTIFY currentFrameChanged)
    /// "H:MM:SS.CC" like the ASS time fields.
    Q_PROPERTY(QString currentTimeText READ currentTimeText NOTIFY currentFrameChanged)
    Q_PROPERTY(double fps READ fps NOTIFY videoInfoChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoInfoChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoInfoChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// Seek the video to the start of the active subtitle line when the grid
    /// selection changes.
    Q_PROPERTY(bool autoSeekToActiveLine READ autoSeekToActiveLine WRITE
                   setAutoSeekToActiveLine NOTIFY autoSeekToActiveLineChanged)

public:
    VideoViewModel(AssFile *file, SubtitleGridModel *gridModel, UndoManager *undoManager,
                   QObject *parent = nullptr);
    ~VideoViewModel() override;

    bool hasVideo() const;
    QString videoName() const;
    QString filePath() const;
    int frameCount() const;
    int currentFrame() const;
    int currentTimeMs() const;
    QString currentTimeText() const;
    double fps() const;
    int videoWidth() const;
    int videoHeight() const;
    bool isPlaying() const;
    QString lastError() const;
    bool autoSeekToActiveLine() const;
    void setAutoSeekToActiveLine(bool enabled);

    /// Open a video file picked by QML's FileDialog.
    Q_INVOKABLE bool openVideo(const QUrl &url);
    /// Open a video file by local path (ffms2 formats).
    Q_INVOKABLE bool openLocalPath(const QString &path);
    /// Open the placeholder/checkerboard video. Parameters <= 0 mean "use the
    /// script's PlayResX/PlayResY, then the 1280x720 / 23.976 fps / 36000 frame
    /// defaults".
    Q_INVOKABLE bool openDummyVideo(int width = 1280, int height = 720,
                                    double fps = 23.976, int length = 36000);
    Q_INVOKABLE void closeVideo();

    Q_INVOKABLE void seekFrame(int frame);
    Q_INVOKABLE void stepFrame(int delta);
    Q_INVOKABLE void seekToTimeMs(int ms);
    Q_INVOKABLE void seekToActiveLine();
    Q_INVOKABLE void togglePlay();

    /// New/Open replaced the document: refresh the libass track.
    Q_INVOKABLE void handleDocumentReload();

signals:
    void hasVideoChanged();
    void videoNameChanged();
    void videoInfoChanged();
    void currentFrameChanged();
    void isPlayingChanged();
    void lastErrorChanged();
    void autoSeekToActiveLineChanged();
    /// Delivers the fully composited (video + subtitles) frame to the viewport.
    void frameReady(const QImage &frame);

private:
    void onVideoOpened(const QString &name);
    void setCurrentFrame(int frame, bool redraw);
    void renderCurrentFrame();
    void setError(const QString &message);
    void resetError();

    void stopPlayback();
    void onPlaybackTick();
    void onFileCommitted(int type, const AssDialogue *single_line);
    void onRestored();
    void onActiveRowChanged();
    void scheduleSubtitleReload();
    void reloadSubtitlesAndRedraw();
    /// Start time of the grid's active line in milliseconds, or -1.
    int activeLineStartMs() const;

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    UndoManager *undo_manager_;
    std::unique_ptr<aegisub_qt::VideoBridge> bridge_;

    QString video_name_;
    QString file_path_;
    QString last_error_;
    int current_frame_ = -1;
    bool playing_ = false;
    bool auto_seek_ = true;

    QTimer playback_timer_;
    QElapsedTimer playback_elapsed_;
    int playback_start_ms_ = 0;

    /// Debounces the libass reload while the user types (one commit per key).
    QTimer subtitle_reload_timer_;

    agi::signal::Connection commit_connection_;
};
