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
#include <QVariantList>

#include <memory>
#include <vector>

class AssDialogue;
class AssFile;
class AudioViewModel;
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
    /// Keyframe start times in milliseconds, as a QML-friendly list.
    Q_PROPERTY(QVariantList keyframeTimesMs READ keyframeTimesMs NOTIFY keyframeTimesChanged)
    Q_PROPERTY(int keyframeCount READ keyframeCount NOTIFY keyframeTimesChanged)
    /// True when the current frame is a keyframe (drives the KF badge).
    Q_PROPERTY(bool currentFrameIsKeyframe READ currentFrameIsKeyframe
                   NOTIFY currentFrameIsKeyframeChanged)

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

    QVariantList keyframeTimesMs() const;
    int keyframeCount() const;
    bool currentFrameIsKeyframe() const;
    /// Raw keyframe list for main.cpp (wired into AudioViewModel).
    std::vector<int> const &keyframeTimesMsVector() const;

    /// Connect the audio view model for A/V-synchronised playback.
    ///
    /// Deliberately not a Q_INVOKABLE: main.cpp owns both view models and calls
    /// this once after constructing them.
    void setAudioViewModel(AudioViewModel *audio);

    /// Frame/time mapping used by the Shift Times frame mode. `end` selects the
    /// agi::vfr::END semantics (last frame on which a line ending at that time
    /// is visible / a time inside that frame) instead of the START semantics.
    Q_INVOKABLE int frameAtTimeMs(int ms, bool end) const;
    Q_INVOKABLE int timeAtFrameMs(int frame, bool end) const;

    /// Open a video file picked by QML's FileDialog.
    ///
    /// When the file has more than one video track this does not open it: a
    /// `trackSelectionRequired` signal asks QML to show the Qt track picker, and
    /// the chosen track comes back through openLocalPathWithTrack().
    Q_INVOKABLE bool openVideo(const QUrl &url);
    /// Open a video file by local path (ffms2 formats), auto-selecting the
    /// first video track (command line / tests; never shows a picker).
    Q_INVOKABLE bool openLocalPath(const QString &path);
    /// Open a specific ffms2 video track id (from the track picker).
    Q_INVOKABLE bool openLocalPathWithTrack(const QString &path, int trackId);
    /// The user cancelled the track picker: drop the pending request.
    Q_INVOKABLE void cancelPendingTrackSelection();
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

    QSize scriptResolution() const;
    void scheduleVisualSubtitleRefresh();
    void flushVisualSubtitleRefresh();

signals:
    void hasVideoChanged();
    void videoNameChanged();
    void videoInfoChanged();
    void currentFrameChanged();
    void isPlayingChanged();
    void lastErrorChanged();
    void autoSeekToActiveLineChanged();
    void keyframeTimesChanged();
    void currentFrameIsKeyframeChanged();
    /// Multi-track file: ask QML to show the track picker. `tracks` is a list of
    /// {id:int, description:string, label:string} maps (label is display-ready,
    /// e.g. "Track 2 — pcm_s16le").
    void trackSelectionRequired(const QString &path, const QString &mediaKind,
                                const QVariantList &tracks);
    /// Delivers the fully composited (video + subtitles) frame to the viewport.
    void frameReady(const QImage &frame);

private:
    bool openLocalPathImpl(const QString &path, int trackId, bool interactive);
    void onVideoOpened(const QString &name);
    void rebuildKeyframes();
    void updateCurrentFrameIsKeyframe();
    void setCurrentFrame(int frame, bool redraw);
    void renderCurrentFrame();
    void setError(const QString &message);
    void resetError();

    void stopPlayback();
    void onPlaybackTick();
    void onFileCommitted(int type, const AssDialogue *single_line);
    void onRestored();
    void onActiveRowChanged();
    /// The audio file was closed/opened: a synced playback cannot continue.
    void onAudioAvailabilityChanged();
    /// "Play Line" started: pause the video instead of fighting for the device.
    void onAudioLinePlaybackStarted();
    void scheduleSubtitleReload();
    void reloadSubtitlesAndRedraw();
    void loadSubtitleTrack();
    /// Start time of the grid's active line in milliseconds, or -1.
    int activeLineStartMs() const;

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    UndoManager *undo_manager_;
    AudioViewModel *audio_vm_ = nullptr;
    std::unique_ptr<aegisub_qt::VideoBridge> bridge_;

    QString video_name_;
    QString file_path_;
    QString last_error_;
    QString pending_track_path_;
    int current_frame_ = -1;
    bool playing_ = false;
    bool auto_seek_ = true;
    bool current_frame_is_keyframe_ = false;
    std::vector<int> keyframe_times_ms_;
    /// True while playback is clocked by the audio player instead of the wall
    /// clock (audio track open and playFrom() succeeded).
    bool audio_sync_ = false;

    QTimer playback_timer_;
    QElapsedTimer playback_elapsed_;
    int playback_start_ms_ = 0;

    /// Debounces the libass reload while the user types (one commit per key).
    QTimer subtitle_reload_timer_;
    bool visual_reload_pending_ = false;

    agi::signal::Connection commit_connection_;
};
