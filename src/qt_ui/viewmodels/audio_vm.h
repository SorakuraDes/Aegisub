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

    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY audioChanged)
    Q_PROPERTY(QString audioName READ audioName NOTIFY audioChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY audioChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY audioChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(int playbackTimeMs READ playbackTimeMs NOTIFY playbackTimeChanged)
    Q_PROPERTY(double zoomPxPerSec READ zoomPxPerSec NOTIFY viewChanged)
    Q_PROPERTY(int viewStartMs READ viewStartMs NOTIFY viewChanged)
    Q_PROPERTY(int activeStartMs READ activeStartMs NOTIFY activeTimesChanged)
    Q_PROPERTY(int activeEndMs READ activeEndMs NOTIFY activeTimesChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// "XAudio2" or the reason the wall-clock silent fallback is active.
    Q_PROPERTY(QString playerStatus READ playerStatus NOTIFY playerStatusChanged)

public:
    AudioViewModel(AssFile *file, SubtitleGridModel *gridModel, UndoManager *undoManager,
                   QObject *parent = nullptr);
    ~AudioViewModel() override;

    bool hasAudio() const;
    QString audioName() const;
    int durationMs() const;
    int sampleRate() const;
    bool isPlaying() const;
    int playbackTimeMs() const;
    double zoomPxPerSec() const;
    int viewStartMs() const;
    int activeStartMs() const;
    int activeEndMs() const;
    double volume() const;
    void setVolume(double volume);
    QString lastError() const;
    QString playerStatus() const;

    Q_INVOKABLE bool openAudio(const QUrl &url);
    Q_INVOKABLE bool openLocalPath(const QString &path);
    Q_INVOKABLE bool openDummyAudio();
    Q_INVOKABLE void closeAudio();

    Q_INVOKABLE void playSelection();
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
    Q_INVOKABLE void setViewportWidthPx(int width);
    Q_INVOKABLE int viewportWidthPx() const;

    /// Peak extraction for AudioWaveformItem (not exposed to QML).
    void computePeaks(int64_t start_sample, int64_t end_sample, int width,
                      std::vector<float> &out_min, std::vector<float> &out_max) const;
    int64_t msToSample(int ms) const;
    int sampleToMs(int64_t sample) const;

    /// New/Open replaced the document: refresh the active line.
    Q_INVOKABLE void handleDocumentReload();

signals:
    void audioChanged();
    void playingChanged();
    void playbackTimeChanged();
    void viewChanged();
    void activeTimesChanged();
    void volumeChanged();
    void lastErrorChanged();
    void playerStatusChanged();

private:
    void refreshActiveTimes();
    void setError(const QString &message);
    void resetError();
    void stopPlayback();
    void onPlaybackTick();
    void onFileCommitted(int type, const AssDialogue *single_line);
    void onRestored();
    void onActiveRowChanged();
    int visibleMs() const;
    void clampView();

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    UndoManager *undo_manager_;
    aegisub_qt::AudioBridge bridge_;

    QString audio_name_;
    QString last_error_;
    int active_start_ms_ = 0;
    int active_end_ms_ = 0;
    int view_start_ms_ = 0;
    double zoom_px_per_sec_ = 100.0;
    int viewport_width_px_ = 800;
    int playback_time_ms_ = 0;
    bool playing_ = false;
    double volume_ = 1.0;

    QTimer playback_timer_;
    agi::signal::Connection commit_connection_;
};
