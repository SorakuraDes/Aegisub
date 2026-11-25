#pragma once

// agi::signal (pulled in by ass_file.h) has a member named `slots`, which Qt's
// `#define slots` would mangle; parse libaegisub before any Qt header. See
// undo_manager.h.
#include <libaegisub/signal.h>

#include "audio_bridge.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <cstdint>
#include <vector>

class AssDialogue;
class AssFile;
class SubtitleGridModel;
class UndoManager;

/// View model for the M2b audio waveform panel.
///
/// Owns an aegisub_qt::AudioBridge (ffms2 audio decode + XAudio2 playback) and
/// exposes the waveform viewport state plus the timing-commit operation to QML.
/// The waveform is painted by AudioWaveformItem, which asks this class for
/// min/max peaks and maps x coordinates to milliseconds.
class AudioViewModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool spectrumMode READ spectrumMode WRITE setSpectrumMode NOTIFY spectrumModeChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY audioChanged)
    Q_PROPERTY(QString audioName READ audioName NOTIFY audioChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY audioChanged)
    Q_PROPERTY(bool isAudioFromVideo READ isAudioFromVideo NOTIFY audioChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY audioChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY audioChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(int playbackTimeMs READ playbackTimeMs NOTIFY playbackTimeChanged)
    Q_PROPERTY(bool followPlayhead READ followPlayhead WRITE setFollowPlayhead
                   NOTIFY followPlayheadChanged)
    Q_PROPERTY(double zoomPxPerSec READ zoomPxPerSec NOTIFY viewChanged)
    Q_PROPERTY(int viewStartMs READ viewStartMs NOTIFY viewChanged)
    Q_PROPERTY(int activeStartMs READ activeStartMs NOTIFY activeTimesChanged)
    Q_PROPERTY(int activeEndMs READ activeEndMs NOTIFY activeTimesChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// "XAudio2" or the reason the wall-clock silent fallback is active.
    Q_PROPERTY(QString playerStatus READ playerStatus NOTIFY playerStatusChanged)
    /// Display-ready description of the selected track ("Track 2 — pcm_s16le");
    /// empty for single-track files and the synthetic dummy audio.
    Q_PROPERTY(QString trackLabel READ trackLabel NOTIFY audioChanged)
    /// Snap waveform Start/End edits to the nearest video keyframe.
    Q_PROPERTY(bool snapToKeyframes READ snapToKeyframes WRITE setSnapToKeyframes
                   NOTIFY snapToKeyframesChanged)
    /// Number of keyframe times received from the video view model.
    Q_PROPERTY(int keyframeCount READ keyframeCount NOTIFY keyframeTimesChanged)

public:
    AudioViewModel(AssFile *file, SubtitleGridModel *gridModel, UndoManager *undoManager,
                   QObject *parent = nullptr);
    ~AudioViewModel() override;

    bool spectrumMode() const { return spectrum_mode_; }
    void setSpectrumMode(bool enabled);
    int64_t decodedSamples() const { return bridge_.DecodedSamples(); }
    void computeSpectrum(int64_t start, int64_t end, int width, std::vector<float> &levels) const;
    bool hasAudio() const;
    QString audioName() const;
    QString filePath() const { return source_path_; }
    bool isAudioFromVideo() const { return source_from_video_; }
    int durationMs() const;
    int sampleRate() const;
    bool isPlaying() const;
    int playbackTimeMs() const;
    bool followPlayhead() const { return follow_playhead_; }
    void setFollowPlayhead(bool enabled);
    double zoomPxPerSec() const;
    int viewStartMs() const;
    int activeStartMs() const;
    int activeEndMs() const;
    double volume() const;
    void setVolume(double volume);
    QString lastError() const;
    QString playerStatus() const;
    QString trackLabel() const;
    bool snapToKeyframes() const;
    void setSnapToKeyframes(bool enabled);
    int snapDistancePx() const;
    bool wheelDefaultsToZoom() const;
    int keyframeCount() const;

    /// Keyframe start times (ms, ascending) from the open video.
    void setKeyframeTimesMs(const std::vector<int> &times);
    std::vector<int> const &keyframeTimesMs() const;
    /// Nearest keyframe within `toleranceMs` of `targetMs` (binary search), or
    /// -1 when there is none in range.
    int nearestKeyframeMs(int targetMs, int toleranceMs) const;

    Q_INVOKABLE bool openAudio(const QUrl &url);
    Q_INVOKABLE bool openLocalPath(const QString &path);
    /// Follow a newly opened video without replacing explicitly opened audio.
    /// An empty path clears stale automatic audio without trying to load any.
    void updateAudioFromVideo(const QString &path, bool autoLoad);
    /// Open a specific ffms2 audio track id (from the Qt track picker).
    Q_INVOKABLE bool openLocalPathWithTrack(const QString &path, int trackId);
    /// Re-open the current file through the track picker ("Select Audio Track").
    /// Single-track files are re-opened directly without any dialog.
    Q_INVOKABLE bool openAudioTrackPicker();
    /// The user cancelled the track picker: drop the pending request.
    Q_INVOKABLE void cancelPendingTrackSelection();
    Q_INVOKABLE bool openDummyAudio();
    Q_INVOKABLE void closeAudio();

    Q_INVOKABLE void playSelection();
    /// Play from `start_ms` to the end of the audio stream. Used by the video
    /// transport so the audio clock drives the video frames. Returns false (and
    /// leaves the player stopped) when playback could not be started.
    Q_INVOKABLE bool playFrom(int start_ms);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void zoomBy(double factor);
    Q_INVOKABLE void scrollByMs(int delta_ms);
    Q_INVOKABLE void centerOnActiveLine();
    /// Clamp the range, write Start/End of the active line and push one
    /// COMMIT_DIAG_TIME commit (single undo step; grid/edit box/video react
    /// through the normal AssFile::Commit bus).
    Q_INVOKABLE void commitLineTiming(int new_start_ms, int new_end_ms);
    Q_INVOKABLE QString formatTime(int ms) const;

    /// The waveform widget reports its width so the view model can clamp and
    /// centre the view without knowing the QML layout.
    Q_INVOKABLE void setViewportWidthPx(double width);
    Q_INVOKABLE double viewportWidthPx() const;
    int visibleMs() const;

    /// Peak extraction for AudioWaveformItem (not exposed to QML).
    void computePeaks(int64_t start_sample, int64_t end_sample, int width,
                      std::vector<float> &out_min, std::vector<float> &out_max) const;
    int64_t msToSample(int ms) const;
    int sampleToMs(int64_t sample) const;

    /// New/Open replaced the document: refresh the active line.
    Q_INVOKABLE void handleDocumentReload();

signals:
    void spectrumModeChanged();
    void audioChanged();
    void fileOpened(const QString &path);
    /// "Play Line" (selection playback) started. The video view model listens to
    /// this so an independent line playback always wins over a video playback
    /// instead of the two transports fighting over the audio device.
    void linePlaybackStarted();
    void playingChanged();
    void playbackTimeChanged();
    void followPlayheadChanged();
    void viewChanged();
    void activeTimesChanged();
    void volumeChanged();
    void lastErrorChanged();
    void playerStatusChanged();
    void snapToKeyframesChanged();
    void keyframeTimesChanged();
    /// Multi-track file: ask QML to show the track picker. `tracks` is a list of
    /// {id:int, description:string, label:string} maps (label is display-ready).
    void trackSelectionRequired(const QString &path, const QString &mediaKind,
                                const QVariantList &tracks);

private:
    bool openLocalPathImpl(const QString &path, int trackId, bool interactive, bool quiet,
                           bool fromVideo = false);
    void refreshActiveTimes();
    void setError(const QString &message);
    void resetError();
    void stopPlayback();
    void onPlaybackTick();
    void onFileCommitted(int type, const AssDialogue *single_line);
    void onRestored();
    void onActiveRowChanged();
    void clampView();
    bool advanceViewForPlayhead();

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    UndoManager *undo_manager_;
    aegisub_qt::AudioBridge bridge_;

    QString audio_name_;
    QString last_error_;
    QString source_path_;
    QString track_label_;
    QString pending_track_path_;
    bool source_from_video_ = false;
    bool pending_track_from_video_ = false;
    int source_track_count_ = 0;
    std::vector<int> keyframe_times_ms_;
    bool spectrum_mode_ = false;
    bool snap_to_keyframes_ = true;
    int active_start_ms_ = 0;
    int active_end_ms_ = 0;
    int view_start_ms_ = 0;
    double zoom_px_per_sec_ = 100.0;
    double viewport_width_px_ = 0.0;
    int playback_time_ms_ = 0;
    int playback_start_ms_ = 0;
    int playback_end_ms_ = 0;
    bool follow_playhead_ = true;
    bool playing_ = false;
    /// True while playSelection() runs (Play Line), false for playFrom().
    bool playing_selection_ = false;
    double volume_ = 1.0;

    QTimer playback_timer_;
    agi::signal::Connection commit_connection_;
    agi::signal::Connection follow_option_;
    agi::signal::Connection snap_option_;
};
