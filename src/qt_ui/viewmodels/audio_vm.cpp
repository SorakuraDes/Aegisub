// ass_file.h first: agi::signal has a member named `slots`, which Qt's macros
// would mangle if a Qt header were parsed first (see docs/qt6-frontend.md #2).
#include "ass_file.h"

#include "ass_dialogue.h"
#include "subtitle_grid_model.h"
#include "undo_manager.h"

#include "audio_vm.h"

#include "../commit_bridge.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>

#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace {

/// QML-friendly track list: each entry is {id, description, label}. `label` is
/// display-ready with a 1-based ordinal ("Track 2 — pcm_s16le").
QVariantList tracksToVariantList(const std::vector<aegisub_qt::MediaTrackInfo> &tracks) {
    QVariantList list;
    list.reserve(int(tracks.size()));
    for (size_t i = 0; i < tracks.size(); ++i) {
        QVariantMap track;
        track.insert(QStringLiteral("id"), tracks[i].id);
        track.insert(QStringLiteral("description"),
                     QString::fromStdString(tracks[i].description));
        track.insert(QStringLiteral("label"),
                     QStringLiteral("Track %1 \u2014 %2")
                         .arg(int(i) + 1)
                         .arg(QString::fromStdString(tracks[i].description)));
        list.append(track);
    }
    return list;
}

QString describeTrack(const std::vector<aegisub_qt::MediaTrackInfo> &tracks, int trackId) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == trackId) {
            return QStringLiteral("Track %1 \u2014 %2")
                .arg(int(i) + 1)
                .arg(QString::fromStdString(tracks[i].description));
        }
    }
    return QString();
}

} // namespace

AudioViewModel::AudioViewModel(AssFile *file, SubtitleGridModel *gridModel,
                               UndoManager *undoManager, QObject *parent)
: QObject(parent)
, file_(file)
, grid_model_(gridModel)
, undo_manager_(undoManager)
{
    playback_timer_.setTimerType(Qt::PreciseTimer);
    playback_timer_.setInterval(16);
    connect(&playback_timer_, &QTimer::timeout, this, &AudioViewModel::onPlaybackTick);

    if (file_)
        commit_connection_ = file_->AddCommitListener(&AudioViewModel::onFileCommitted, this);
    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &AudioViewModel::onRestored);
    if (grid_model_)
        connect(grid_model_, &SubtitleGridModel::activeRowChanged,
                this, &AudioViewModel::onActiveRowChanged);
}

AudioViewModel::~AudioViewModel() {
    playback_timer_.stop();
    bridge_.Stop();
}

bool AudioViewModel::hasAudio() const { return bridge_.IsOpen(); }
QString AudioViewModel::audioName() const { return audio_name_; }
int AudioViewModel::durationMs() const { return bridge_.DurationMs(); }
int AudioViewModel::sampleRate() const { return bridge_.SampleRate(); }
bool AudioViewModel::isPlaying() const { return playing_; }
int AudioViewModel::playbackTimeMs() const { return playback_time_ms_; }
double AudioViewModel::zoomPxPerSec() const { return zoom_px_per_sec_; }
int AudioViewModel::viewStartMs() const { return view_start_ms_; }
int AudioViewModel::activeStartMs() const { return active_start_ms_; }
int AudioViewModel::activeEndMs() const { return active_end_ms_; }
double AudioViewModel::volume() const { return volume_; }
QString AudioViewModel::lastError() const { return last_error_; }
QString AudioViewModel::playerStatus() const {
    return QString::fromStdString(bridge_.PlayerStatus());
}

QString AudioViewModel::trackLabel() const { return track_label_; }

bool AudioViewModel::snapToKeyframes() const { return snap_to_keyframes_; }

void AudioViewModel::setSnapToKeyframes(bool enabled) {
    if (snap_to_keyframes_ == enabled)
        return;
    snap_to_keyframes_ = enabled;
    emit snapToKeyframesChanged();
}

int AudioViewModel::keyframeCount() const { return int(keyframe_times_ms_.size()); }

void AudioViewModel::setKeyframeTimesMs(const std::vector<int> &times) {
    std::vector<int> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (sorted == keyframe_times_ms_)
        return;
    keyframe_times_ms_.swap(sorted);
    emit keyframeTimesChanged();
}

std::vector<int> const &AudioViewModel::keyframeTimesMs() const {
    return keyframe_times_ms_;
}

int AudioViewModel::nearestKeyframeMs(int targetMs, int toleranceMs) const {
    if (keyframe_times_ms_.empty() || toleranceMs < 0)
        return -1;

    auto it = std::lower_bound(keyframe_times_ms_.begin(), keyframe_times_ms_.end(),
                               targetMs);
    int best = -1;
    int best_distance = std::numeric_limits<int>::max();
    if (it != keyframe_times_ms_.end()) {
        best = *it;
        best_distance = std::abs(*it - targetMs);
    }
    if (it != keyframe_times_ms_.begin()) {
        --it;
        const int distance = std::abs(*it - targetMs);
        if (distance < best_distance) {
            best = *it;
            best_distance = distance;
        }
    }
    return best_distance <= toleranceMs ? best : -1;
}

void AudioViewModel::setVolume(double volume) {
    const double clamped = std::clamp(volume, 0.0, 1.0);
    if (volume_ == clamped)
        return;
    volume_ = clamped;
    bridge_.SetVolume(volume_);
    emit volumeChanged();
}

bool AudioViewModel::openAudio(const QUrl &url) {
    if (url.isLocalFile())
        return openLocalPathImpl(url.toLocalFile(), -1, true, false);
    setError(QStringLiteral("Only local files are supported"));
    return false;
}

bool AudioViewModel::openLocalPath(const QString &path) {
    return openLocalPathImpl(path, -1, false, false);
}

bool AudioViewModel::openLocalPathQuiet(const QString &path) {
    // Never replace an explicitly opened audio file, and stay silent on failure:
    // a video without an audio track is a perfectly normal case (the video view
    // model calls this for every real video it opens).
    if (hasAudio())
        return false;
    return openLocalPathImpl(path, -1, false, true);
}

bool AudioViewModel::openLocalPathWithTrack(const QString &path, int trackId) {
    if (trackId < 0) {
        setError(QStringLiteral("No audio track selected"));
        return false;
    }
    return openLocalPathImpl(path, trackId, false, false);
}

bool AudioViewModel::openAudioTrackPicker() {
    if (!hasAudio() || source_path_.isEmpty())
        return false;

    std::string probe_error;
    const auto tracks = aegisub_qt::AudioBridge::ProbeAudioTracks(
        source_path_.toUtf8().toStdString(), &probe_error);
    if (tracks.size() > 1) {
        pending_track_path_ = source_path_;
        emit trackSelectionRequired(pending_track_path_, QStringLiteral("audio"),
                                    tracksToVariantList(tracks));
        return true;
    }

    // Single-track (or unprobeable) file: just re-open it, no dialog.
    const int only_track = tracks.empty() ? -1 : tracks.front().id;
    return openLocalPathImpl(source_path_, only_track, false, false);
}

void AudioViewModel::cancelPendingTrackSelection() {
    pending_track_path_.clear();
}

bool AudioViewModel::openLocalPathImpl(const QString &path, int trackId, bool interactive,
                                       bool quiet) {
    if (path.isEmpty()) {
        if (!quiet)
            setError(QStringLiteral("No audio path given"));
        return false;
    }

    const QFileInfo info(path);
    if (!info.isFile()) {
        if (!quiet)
            setError(QStringLiteral("Audio file not found: ") + path);
        return false;
    }

    // Two-phase track selection (see VideoViewModel::openLocalPathImpl): probe
    // the container, ask QML when interactive and multi-track, then open the
    // chosen ffms2 track id. The provider never shows a wx dialog.
    std::string probe_error;
    const auto tracks = aegisub_qt::AudioBridge::ProbeAudioTracks(
        path.toUtf8().toStdString(), &probe_error);
    if (tracks.empty()) {
        if (quiet) {
            LOG_D("qt/audio") << "automatic audio load skipped:"
                              << (probe_error.empty() ? "no audio tracks" : probe_error);
            return false;
        }
        setError(probe_error.empty() ? QStringLiteral("No audio tracks found")
                                     : QString::fromStdString(probe_error));
        return false;
    }

    int chosen_track = trackId;
    if (chosen_track < 0) {
        if (interactive && tracks.size() > 1) {
            pending_track_path_ = info.absoluteFilePath();
            emit trackSelectionRequired(pending_track_path_, QStringLiteral("audio"),
                                        tracksToVariantList(tracks));
            return false;
        }
        chosen_track = tracks.front().id;
    }
    pending_track_path_.clear();

    stopPlayback();

    std::string error;
    if (!bridge_.OpenFile(path.toUtf8().toStdString(), &error, chosen_track)) {
        // AudioBridge::OpenFile() closes itself on failure.
        if (quiet) {
            LOG_D("qt/audio") << "automatic audio load skipped:" << error;
            return false;
        }
        audio_name_.clear();
        source_path_.clear();
        track_label_.clear();
        source_track_count_ = 0;
        setError(QString::fromStdString(error));
        emit audioChanged();
        emit playerStatusChanged();
        return false;
    }

    audio_name_ = info.fileName();
    source_path_ = info.absoluteFilePath();
    source_track_count_ = int(tracks.size());
    track_label_ = tracks.size() > 1 ? describeTrack(tracks, chosen_track) : QString();
    view_start_ms_ = 0;
    playback_time_ms_ = 0;
    if (!quiet)
        resetError();
    refreshActiveTimes();
    centerOnActiveLine();

    emit audioChanged();
    emit playerStatusChanged();
    emit activeTimesChanged();
    emit viewChanged();
    emit playbackTimeChanged();
    return true;
}

bool AudioViewModel::openDummyAudio() {
    stopPlayback();

    std::string error;
    if (!bridge_.OpenDummy(10, &error)) {
        setError(QString::fromStdString(error));
        return false;
    }

    audio_name_ = QStringLiteral("Dummy audio (10 s, 440 Hz sine)");
    source_path_.clear();
    track_label_.clear();
    source_track_count_ = 0;
    view_start_ms_ = 0;
    playback_time_ms_ = 0;
    resetError();
    refreshActiveTimes();
    centerOnActiveLine();

    emit audioChanged();
    emit playerStatusChanged();
    emit activeTimesChanged();
    emit viewChanged();
    emit playbackTimeChanged();
    return true;
}

void AudioViewModel::closeAudio() {
    stopPlayback();
    bridge_.Close();
    audio_name_.clear();
    source_path_.clear();
    track_label_.clear();
    source_track_count_ = 0;
    pending_track_path_.clear();
    playback_time_ms_ = 0;
    view_start_ms_ = 0;
    zoom_px_per_sec_ = 100.0;

    emit audioChanged();
    emit playerStatusChanged();
    emit playbackTimeChanged();
    emit viewChanged();
}

void AudioViewModel::playSelection() {
    if (!hasAudio())
        return;

    stopPlayback();
    // Tell the video view model that an independent (Play Line) playback is
    // taking over the audio device; it pauses its own transport instead of the
    // two of them interleaving.
    emit linePlaybackStarted();
    refreshActiveTimes();

    int start_ms = active_start_ms_;
    int end_ms = active_end_ms_;
    if (end_ms <= start_ms)
        end_ms = start_ms + 500;

    const int duration = durationMs();
    if (duration <= 0)
        return;

    start_ms = std::clamp(start_ms, 0, std::max(0, duration - 1));
    end_ms = std::clamp(end_ms, start_ms + 1, duration);

    std::string error;
    if (!bridge_.PlayRange(msToSample(start_ms), msToSample(end_ms), &error)) {
        setError(QString::fromStdString(error));
        emit playerStatusChanged();
        return;
    }

    playing_ = true;
    playing_selection_ = true;
    playback_time_ms_ = start_ms;
    playback_timer_.start();
    emit playingChanged();
    emit playbackTimeChanged();
    // PlayRange may have switched to the silent fallback.
    emit playerStatusChanged();
}

bool AudioViewModel::playFrom(int start_ms) {
    if (!hasAudio())
        return false;

    stopPlayback();

    const int duration = durationMs();
    if (duration <= 0)
        return false;

    const int start = std::clamp(start_ms, 0, std::max(0, duration - 1));

    std::string error;
    if (!bridge_.PlayRange(msToSample(start), msToSample(duration), &error)) {
        setError(QString::fromStdString(error));
        emit playerStatusChanged();
        return false;
    }

    playing_ = true;
    // Full-stream playback: the playhead is not clamped to the active line.
    playing_selection_ = false;
    playback_time_ms_ = start;
    playback_timer_.start();
    emit playingChanged();
    emit playbackTimeChanged();
    emit playerStatusChanged();
    return true;
}

void AudioViewModel::stop() {
    if (!playing_)
        return;
    stopPlayback();
}

void AudioViewModel::zoomBy(double factor) {
    if (!hasAudio() || factor <= 0.0)
        return;

    const int visible = visibleMs();
    const double center_ms = view_start_ms_ + visible / 2.0;
    const double new_zoom = std::clamp(zoom_px_per_sec_ * factor, 20.0, 800.0);
    if (new_zoom == zoom_px_per_sec_)
        return;

    zoom_px_per_sec_ = new_zoom;
    const int new_visible = visibleMs();
    view_start_ms_ = std::clamp(int(center_ms - new_visible / 2.0), 0,
                                std::max(0, durationMs() - new_visible));
    emit viewChanged();
}

void AudioViewModel::scrollByMs(int delta_ms) {
    if (!hasAudio() || delta_ms == 0)
        return;

    const int visible = visibleMs();
    const int target = std::clamp(view_start_ms_ + delta_ms, 0,
                                  std::max(0, durationMs() - visible));
    if (target == view_start_ms_)
        return;

    view_start_ms_ = target;
    emit viewChanged();
}

void AudioViewModel::centerOnActiveLine() {
    if (!hasAudio())
        return;

    const int visible = visibleMs();
    const int center = active_end_ms_ > active_start_ms_
        ? active_start_ms_ + (active_end_ms_ - active_start_ms_) / 2
        : active_start_ms_;
    const int target = std::clamp(center - visible / 2, 0,
                                  std::max(0, durationMs() - visible));
    if (target == view_start_ms_)
        return;

    view_start_ms_ = target;
    emit viewChanged();
}

void AudioViewModel::commitLineTiming(int new_start_ms, int new_end_ms) {
    if (!file_ || !grid_model_)
        return;

    AssDialogue *line = grid_model_->getActiveDialogue();
    if (!line)
        return;

    if (playing_)
        stopPlayback();

    const int start = std::max(0, new_start_ms);
    const int end = std::max(start, new_end_ms);
    if (start == static_cast<int>(line->Start) && end == static_cast<int>(line->End)) {
        // Nothing changed; still refresh the preview values.
        active_start_ms_ = start;
        active_end_ms_ = end;
        emit activeTimesChanged();
        return;
    }

    line->Start = start;
    line->End = end;
    // One commit = one undo step; the grid / edit box / video view model all
    // subscribe to the same AssFile::Commit bus.
    aegisub_qt::commit(file_, "Set line timing", AssFile::COMMIT_DIAG_TIME, -1, line);

    active_start_ms_ = start;
    active_end_ms_ = end;
    emit activeTimesChanged();
    emit viewChanged();
}

QString AudioViewModel::formatTime(int ms) const {
    return QString::fromStdString(agi::Time(std::max(0, ms)).GetAssFormatted());
}

void AudioViewModel::setViewportWidthPx(int width) {
    const int clamped = std::max(100, width);
    if (clamped == viewport_width_px_)
        return;

    viewport_width_px_ = clamped;
    clampView();
    emit viewChanged();
}

int AudioViewModel::viewportWidthPx() const { return viewport_width_px_; }

void AudioViewModel::computePeaks(int64_t start_sample, int64_t end_sample, int width,
                                  std::vector<float> &out_min,
                                  std::vector<float> &out_max) const {
    bridge_.ComputePeaks(start_sample, end_sample, width, out_min, out_max);
}

int64_t AudioViewModel::msToSample(int ms) const {
    const int rate = sampleRate();
    if (rate <= 0)
        return 0;
    return int64_t(std::max(0, ms)) * rate / 1000;
}

int AudioViewModel::sampleToMs(int64_t sample) const {
    const int rate = sampleRate();
    if (rate <= 0)
        return 0;
    return int(std::max<int64_t>(0, sample) * 1000 / rate);
}

void AudioViewModel::handleDocumentReload() {
    refreshActiveTimes();
    if (hasAudio())
        centerOnActiveLine();
    emit activeTimesChanged();
    emit viewChanged();
}

void AudioViewModel::refreshActiveTimes() {
    int start = 0;
    int end = 0;
    if (grid_model_) {
        const AssDialogue *line = grid_model_->getActiveDialogue();
        if (line) {
            start = static_cast<int>(line->Start);
            end = static_cast<int>(line->End);
        }
    }

    active_start_ms_ = std::max(0, start);
    active_end_ms_ = std::max(active_start_ms_, end);
}

void AudioViewModel::setError(const QString &message) {
    if (last_error_ == message)
        return;
    last_error_ = message;
    LOG_E("qt/audio") << message.toStdString();
    emit lastErrorChanged();
}

void AudioViewModel::resetError() {
    if (last_error_.isEmpty())
        return;
    last_error_.clear();
    emit lastErrorChanged();
}

int AudioViewModel::visibleMs() const {
    const double visible = viewport_width_px_ * 1000.0 / zoom_px_per_sec_;
    return std::max(1000, int(visible));
}

void AudioViewModel::clampView() {
    const int visible = visibleMs();
    view_start_ms_ = std::clamp(view_start_ms_, 0, std::max(0, durationMs() - visible));
}

void AudioViewModel::stopPlayback() {
    bridge_.Stop();
    playback_timer_.stop();

    const bool was_selection = playing_selection_;
    playing_selection_ = false;

    if (playing_) {
        playing_ = false;
        emit playingChanged();
    }

    // Play Line parks the playhead at the line start for the next attempt;
    // full-stream playback (video sync) just stops where it is.
    if (hasAudio() && was_selection) {
        playback_time_ms_ = std::max(active_start_ms_, playback_time_ms_);
        emit playbackTimeChanged();
    }
}

void AudioViewModel::onPlaybackTick() {
    if (!playing_)
        return;

    if (!bridge_.IsPlaying()) {
        playback_time_ms_ = playing_selection_ ? active_end_ms_ : durationMs();
        emit playbackTimeChanged();
        stopPlayback();
        return;
    }

    const int64_t sample = bridge_.CurrentSample();
    int ms = sampleToMs(sample);
    if (playing_selection_)
        ms = std::clamp(ms, active_start_ms_, std::max(active_start_ms_, active_end_ms_));
    else
        ms = std::clamp(ms, 0, std::max(0, durationMs()));
    if (ms != playback_time_ms_) {
        playback_time_ms_ = ms;
        emit playbackTimeChanged();
    }
}

void AudioViewModel::onFileCommitted(int, const AssDialogue *) {
    refreshActiveTimes();
    emit activeTimesChanged();
}

void AudioViewModel::onRestored() {
    // Undo/redo swapped the whole AssFile; defer until the grid has rebuilt its
    // row index (same pattern as EditBoxViewModel).
    QTimer::singleShot(0, this, [this] {
        refreshActiveTimes();
        emit activeTimesChanged();
        emit viewChanged();
    });
}

void AudioViewModel::onActiveRowChanged() {
    if (playing_)
        stopPlayback();

    refreshActiveTimes();
    emit activeTimesChanged();

    if (hasAudio()) {
        const int visible = visibleMs();
        const bool outside = active_end_ms_ < view_start_ms_
            || active_start_ms_ > view_start_ms_ + visible;
        if (outside)
            centerOnActiveLine();

        // Park the playhead at the new line start so the next Play Line starts
        // exactly where the red line is.
        playback_time_ms_ = active_start_ms_;
        emit playbackTimeChanged();
    }
}
