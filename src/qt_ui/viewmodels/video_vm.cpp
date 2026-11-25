// ass_file.h first: agi::signal has a member named `slots`, which Qt's macros
// would mangle if a Qt header were parsed first (see docs/qt6-frontend.md #2).
#include "ass_file.h"

#include "ass_attachment.h"
#include "ass_dialogue.h"

#include "subtitle_grid_model.h"
#include "undo_manager.h"

// config::path (for the ffms2 index cache directory) and the same config.json
// the wxWidgets frontend uses.
#include "../../options.h"

#include "video_vm.h"

#include "audio_vm.h"

#include "../video_bridge.h"
#include "../commit_bridge.h"
#include "../qt_path.h"
#include "../aegisub_core.h"
#include "../ffmpegsource_common_qt.h"

#include <QRegularExpression>
#include <limits>

#include <libaegisub/ass/time.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>
#include <cstring>
#include <exception>
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

} // namespace

namespace {

/// agi::fs::CreateDirectory can fail-fast in this environment (see
/// docs/qt6-frontend.md pitfall 5), and FFmpegSourceProvider creates the cache
/// directory itself while opening a file. Pre-create it with QDir so the
/// original provider only ever sees an existing directory.
void ensureFfms2CacheDirectory() {
#ifdef _WIN32
    try {
        if (!config::path)
            return;
        const QString directory =
            QString::fromStdWString(config::path->Decode("?local/ffms2cache").wstring());
        if (!QDir().mkpath(directory))
            qWarning() << "[video] could not create the ffms2 cache directory" << directory;
    } catch (...) {
        // Not being able to prepare the cache only means ffms2 will index the
        // file again on the next open; it must not abort opening the video.
    }
#endif
}

} // namespace

VideoViewModel::VideoViewModel(AssFile *file, SubtitleGridModel *gridModel,
                               UndoManager *undoManager, QObject *parent)
: QObject(parent)
, file_(file)
, grid_model_(gridModel)
, undo_manager_(undoManager)
, bridge_(std::make_unique<aegisub_qt::VideoBridge>())
{
    if (config::opt) {
        ffms_log_option_ = OPT_SUB("Provider/FFmpegSource/Log Level", &aegisub_qt::ApplyFfmsLogLevel);
        auto_seek_ = OPT_GET("Video/Subtitle Sync")->GetBool();
        auto_seek_option_ = OPT_SUB("Video/Subtitle Sync", [this] {
            setAutoSeekToActiveLine(OPT_GET("Video/Subtitle Sync")->GetBool());
        });
    }
    playback_timer_.setTimerType(Qt::PreciseTimer);
    connect(&playback_timer_, &QTimer::timeout, this, &VideoViewModel::onPlaybackTick);

    subtitle_reload_timer_.setSingleShot(true);
    connect(&subtitle_reload_timer_, &QTimer::timeout,
            this, &VideoViewModel::reloadSubtitlesAndRedraw);

    if (file_)
        commit_connection_ = file_->AddCommitListener(&VideoViewModel::onFileCommitted, this);
    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &VideoViewModel::onRestored);
    if (grid_model_)
        connect(grid_model_, &SubtitleGridModel::activeRowChanged,
                this, &VideoViewModel::onActiveRowChanged);
}

VideoViewModel::~VideoViewModel() {
    playback_timer_.stop();
    subtitle_reload_timer_.stop();
    bridge_.reset();
}

bool VideoViewModel::hasVideo() const { return bridge_->IsOpen(); }
QString VideoViewModel::videoName() const { return video_name_; }
QString VideoViewModel::filePath() const { return file_path_; }
int VideoViewModel::frameCount() const { return bridge_->FrameCount(); }
int VideoViewModel::currentFrame() const { return current_frame_; }
bool VideoViewModel::isPlaying() const { return playing_; }
QString VideoViewModel::lastError() const { return last_error_; }
bool VideoViewModel::autoSeekToActiveLine() const { return auto_seek_; }
double VideoViewModel::fps() const { return bridge_->FPS(); }
int VideoViewModel::videoWidth() const { return bridge_->Width(); }
double VideoViewModel::sourceDisplayAspectRatio() const { return bridge_->DisplayAspectRatio(); }
std::vector<int> VideoViewModel::sourceKeyframes() const { return bridge_->GetKeyframes(); }
int VideoViewModel::videoHeight() const { return bridge_->Height(); }

int VideoViewModel::currentTimeMs() const {
    if (!hasVideo() || current_frame_ < 0)
        return 0;
    return bridge_->TimeAtFrame(current_frame_);
}

QString VideoViewModel::currentTimeText() const {
    return QString::fromStdString(agi::Time(currentTimeMs()).GetAssFormatted());
}

void VideoViewModel::setZoomPercent(int percent) {
    if ((percent != 0 && percent != 50 && percent != 100 && percent != 200) ||
        zoom_percent_ == percent)
        return;
    zoom_percent_ = percent;
    emit zoomPercentChanged();
}

void VideoViewModel::setAutoSeekToActiveLine(bool enabled) {
    if (config::opt && OPT_GET("Video/Subtitle Sync")->GetBool() != enabled) {
        OPT_SET("Video/Subtitle Sync")->SetBool(enabled);
        aegisub_qt::FlushOptions();
        return;
    }
    if (auto_seek_ == enabled)
        return;

    auto_seek_ = enabled;
    emit autoSeekToActiveLineChanged();

    if (enabled && hasVideo() && !playing_)
        seekToActiveLine();
}

bool VideoViewModel::openVideo(const QUrl &url) {
    if (url.isLocalFile())
        return openLocalPathImpl(url.toLocalFile(), -1, true);
    setError(QStringLiteral("Only local files are supported"));
    return false;
}

bool VideoViewModel::openLocalPath(const QString &path) {
    return openLocalPathImpl(path, -1, false);
}

bool VideoViewModel::openLocalPathWithTrack(const QString &path, int trackId) {
    if (trackId < 0) {
        setError(QStringLiteral("No video track selected"));
        return false;
    }
    return openLocalPathImpl(path, trackId, false);
}

void VideoViewModel::cancelPendingTrackSelection() {
    pending_track_path_.clear();
}

bool VideoViewModel::openLocalPathImpl(const QString &path, int trackId, bool interactive) {
    if (path.isEmpty()) {
        setError(QStringLiteral("No video path given"));
        return false;
    }

    const QFileInfo info(path);
    if (!info.isFile()) {
        setError(QStringLiteral("Video file not found: ") + path);
        return false;
    }

    ensureFfms2CacheDirectory();

    // Two-phase track selection: probe the container first (cheap, no index),
    // ask QML for a track if there is more than one and the caller is an
    // interactive menu action, then open with the chosen ffms2 track id. The
    // provider therefore never has to ask the user itself (no wx dialog).
    const auto native_path = aegisub_qt::ToNativePath(info.absoluteFilePath());
    std::string probe_error;
    const auto tracks = aegisub_qt::VideoBridge::ProbeVideoTracks(native_path, &probe_error);
    if (tracks.empty() && !probe_error.empty()) {
        setError(QString::fromStdString(probe_error));
        return false;
    }

    int chosen_track = trackId;
    if (chosen_track < 0) {
        if (interactive && tracks.size() > 1) {
            pending_track_path_ = info.absoluteFilePath();
            emit trackSelectionRequired(pending_track_path_, QStringLiteral("video"),
                                        tracksToVariantList(tracks));
            return false;
        }
        if (!tracks.empty())
            chosen_track = tracks.front().id;
    }
    pending_track_path_.clear();

    std::string error;
    if (!bridge_->OpenFile(native_path, &error, chosen_track)) {
        setError(QString::fromStdString(error));
        return false;
    }

    file_path_ = info.absoluteFilePath();
    onVideoOpened(info.fileName());

    if (audio_vm_)
        audio_vm_->updateAudioFromVideo(file_path_, !config::opt || OPT_GET("Video/Open Audio")->GetBool());

    emit fileOpened(file_path_);
    return true;
}

bool VideoViewModel::openDummyVideo(int width, int height, double fps_value, int length) {
    int resolved_width = width;
    int resolved_height = height;
    if (resolved_width <= 0 || resolved_height <= 0) {
        if (file_) {
            resolved_width = file_->GetScriptInfoAsInt("PlayResX");
            resolved_height = file_->GetScriptInfoAsInt("PlayResY");
        }
    }
    if (resolved_width <= 0 || resolved_height <= 0) {
        resolved_width = config::opt
            ? int(std::clamp<int64_t>(OPT_GET("Subtitle/Default Resolution/Width")->GetInt(), 1, 16384))
            : 1280;
        resolved_height = config::opt
            ? int(std::clamp<int64_t>(OPT_GET("Subtitle/Default Resolution/Height")->GetInt(), 1, 16384))
            : 720;
    }

    if (fps_value <= 0.0)
        fps_value = 23.976;
    if (length <= 0)
        length = 36000;

    std::string error;
    if (!bridge_->OpenDummy(resolved_width, resolved_height, fps_value, length, &error)) {
        setError(QString::fromStdString(error));
        return false;
    }

    file_path_.clear();
    onVideoOpened(QStringLiteral("Dummy %1x%2 @ %3 fps")
                      .arg(resolved_width)
                      .arg(resolved_height)
                      .arg(fps_value, 0, 'f', 3));
    if (audio_vm_)
        audio_vm_->updateAudioFromVideo({}, false);
    return true;
}

void VideoViewModel::closeVideo() {
    // Also stops a synced audio playback (see stopPlayback()).
    stopPlayback();
    subtitle_reload_timer_.stop();
    visual_reload_pending_ = false;

    bridge_->Close();

    video_name_.clear();
    file_path_.clear();
    current_frame_ = -1;
    pending_track_path_.clear();

    keyframe_times_ms_.clear();
    emit keyframeTimesChanged();
    updateCurrentFrameIsKeyframe();

    emit hasVideoChanged();
    emit videoNameChanged();
    emit videoInfoChanged();
    emit currentFrameChanged();
    emit frameReady(QImage());
}

void VideoViewModel::seekFrame(int frame) {
    if (!hasVideo())
        return;

    // A manual seek stops the transport on both sides of an A/V sync; a half
    // stopped pair (video paused while the audio still runs) must not happen.
    if (playing_)
        stopPlayback();

    const int last = frameCount() - 1;
    const int target = std::clamp(frame, 0, last);

    setCurrentFrame(target, true);
}

void VideoViewModel::stepFrame(int delta) {
    if (!hasVideo())
        return;

    stopPlayback();
    seekFrame(current_frame_ + delta);
}

void VideoViewModel::seekToTimeMs(int ms) {
    if (!hasVideo())
        return;
    seekFrame(bridge_->FrameAtTime(ms));
}

bool VideoViewModel::jumpTo(const QString &value) {
    if (!hasVideo() || frameCount() <= 0)
        return false;

    const QString input = value.trimmed();
    static const QRegularExpression framePattern(QStringLiteral("\\A[0-9]+\\z"));
    if (framePattern.match(input).hasMatch()) {
        bool ok = false;
        const int frame = input.toInt(&ok);
        if (!ok || frame >= frameCount())
            return false;
        seekFrame(frame);
        return true;
    }

    static const QRegularExpression timePattern(
        QStringLiteral("\\A([0-9]+):([0-5][0-9]):([0-5][0-9])\\.([0-9]{2})\\z"));
    const auto match = timePattern.match(input);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    const int hours = match.captured(1).toInt(&ok);
    if (!ok)
        return false;
    const qint64 ms = hours * 3600000LL + match.captured(2).toInt() * 60000LL
        + match.captured(3).toInt() * 1000LL + match.captured(4).toInt() * 10LL;
    if (ms > std::numeric_limits<int>::max())
        return false;
    const int frame = bridge_->FrameAtTime(int(ms));
    if (frame < 0 || frame >= frameCount())
        return false;
    seekFrame(frame);
    return true;
}

void VideoViewModel::seekToActiveLine() {
    if (!hasVideo())
        return;

    const int start_ms = activeLineStartMs();
    if (start_ms < 0)
        return;

    // START semantics: land on the first frame on which the line is visible.
    seekFrame(bridge_->FrameAtTimeStart(start_ms));
}

void VideoViewModel::seekToActiveLineEnd() {
    if (!hasVideo() || !grid_model_)
        return;
    if (const auto *line = grid_model_->getActiveDialogue())
        seekFrame(bridge_->FrameAtTimeEnd(line->End));
}

void VideoViewModel::togglePlay() {
    if (!hasVideo())
        return;

    if (playing_) {
        stopPlayback();
        return;
    }

    if (current_frame_ >= frameCount() - 1)
        seekFrame(0);

    // Prefer the audio clock: when an audio track is open, start it at the
    // current video position and let its playback position drive the frames.
    // Long playbacks then cannot drift apart (wall-clock + decode jitter would
    // accumulate on the video side).
    audio_sync_ = audio_vm_ && audio_vm_->hasAudio() && audio_vm_->playFrom(currentTimeMs());

    playing_ = true;
    playback_start_ms_ = currentTimeMs();
    playback_elapsed_.start();

    const double frames_per_second = fps() > 0.0 ? fps() : 24.0;
    playback_timer_.start(std::max(1, int(1000.0 / frames_per_second + 0.5)));
    emit isPlayingChanged();
}

void VideoViewModel::setAudioViewModel(AudioViewModel *audio) {
    if (audio_vm_ == audio)
        return;

    if (audio_vm_)
        disconnect(audio_vm_, nullptr, this, nullptr);

    audio_vm_ = audio;
    if (!audio_vm_)
        return;

    connect(audio_vm_, &AudioViewModel::audioChanged,
            this, &VideoViewModel::onAudioAvailabilityChanged);
    connect(audio_vm_, &AudioViewModel::linePlaybackStarted,
            this, &VideoViewModel::onAudioLinePlaybackStarted);
}

int VideoViewModel::frameAtTimeMs(int ms, bool end) const {
    if (!hasVideo())
        return 0;
    return end ? bridge_->FrameAtTimeEnd(ms) : bridge_->FrameAtTimeStart(ms);
}

int VideoViewModel::timeAtFrameMs(int frame, bool end) const {
    if (!hasVideo())
        return 0;
    return end ? bridge_->TimeAtFrameEnd(frame) : bridge_->TimeAtFrameStart(frame);
}

void VideoViewModel::handleDocumentReload() {
    if (!hasVideo())
        return;
    reloadSubtitlesAndRedraw();
}

void VideoViewModel::onVideoOpened(const QString &name) {
    video_name_ = name;
    resetError();

    stopPlayback();
    current_frame_ = -1;
    pending_track_path_.clear();

    if (file_ && !file_path_.isEmpty() && config::opt &&
        OPT_GET("Subtitle/Default Resolution/Auto")->GetBool() &&
        videoWidth() > 0 && videoHeight() > 0) {
        const bool missingX = file_->GetScriptInfoAsInt("PlayResX") <= 0;
        const bool missingY = file_->GetScriptInfoAsInt("PlayResY") <= 0;
        if (missingX) file_->SetScriptInfo("PlayResX", std::to_string(videoWidth()));
        if (missingY) file_->SetScriptInfo("PlayResY", std::to_string(videoHeight()));
        if (missingX || missingY)
            aegisub_qt::commit(file_, "set script resolution from video", AssFile::COMMIT_SCRIPTINFO);
    }
    subtitle_reload_timer_.stop();
    visual_reload_pending_ = false;
    loadSubtitleTrack();

    rebuildKeyframes();

    emit hasVideoChanged();
    emit videoNameChanged();
    emit videoInfoChanged();
    emit currentFrameChanged();

    // Open on the active line (like the wxWidgets frontend's auto-seek), or on
    // the first frame when no line is selected.
    const int start_ms = auto_seek_ ? activeLineStartMs() : -1;
    if (start_ms >= 0)
        seekFrame(bridge_->FrameAtTimeStart(start_ms));
    else
        seekFrame(0);
}

void VideoViewModel::setCurrentFrame(int frame, bool redraw) {
    if (current_frame_ == frame) {
        if (redraw)
            renderCurrentFrame();
        return;
    }

    current_frame_ = frame;
    updateCurrentFrameIsKeyframe();
    emit currentFrameChanged();

    if (redraw)
        renderCurrentFrame();
}

QVariantList VideoViewModel::keyframeTimesMs() const {
    QVariantList list;
    list.reserve(int(keyframe_times_ms_.size()));
    for (int ms : keyframe_times_ms_)
        list.append(ms);
    return list;
}

int VideoViewModel::keyframeCount() const {
    return int(keyframe_times_ms_.size());
}

bool VideoViewModel::currentFrameIsKeyframe() const { return current_frame_is_keyframe_; }

std::vector<int> const &VideoViewModel::keyframeTimesMsVector() const {
    return keyframe_times_ms_;
}

void VideoViewModel::rebuildKeyframes() {
    keyframe_times_ms_ = bridge_->GetKeyframeTimesMs();
    emit keyframeTimesChanged();
    updateCurrentFrameIsKeyframe();
}

void VideoViewModel::updateCurrentFrameIsKeyframe() {
    bool is_keyframe = false;
    if (hasVideo() && current_frame_ >= 0 && !keyframe_times_ms_.empty()) {
        // The list stores agi::vfr::START times, so map the current frame the
        // same way to get an exact hit for CFR and VFR alike.
        const int ms = bridge_->TimeAtFrameStart(current_frame_);
        is_keyframe = std::binary_search(keyframe_times_ms_.begin(),
                                         keyframe_times_ms_.end(), ms);
    }

    if (is_keyframe == current_frame_is_keyframe_)
        return;
    current_frame_is_keyframe_ = is_keyframe;
    emit currentFrameIsKeyframeChanged();
}

void VideoViewModel::renderCurrentFrame() {
    if (!hasVideo() || current_frame_ < 0)
        return;

    std::string error;
    if (!bridge_->DecodeFrame(current_frame_, currentTimeMs(), &error)) {
        setError(QString::fromStdString(error));
        return;
    }

    const int width = bridge_->FrameWidth();
    const int height = bridge_->FrameHeight();
    const size_t line_bytes = size_t(width) * 4;
    const auto &data = bridge_->FrameData();
    if (width <= 0 || height <= 0 || data.size() < line_bytes * size_t(height)) {
        setError(QStringLiteral("The decoded frame has an unexpected size"));
        return;
    }

    // Format_ARGB32 on Windows (little endian) maps straight onto the provider's
    // BGRA8 bytes, so this is a plain row copy; pitch is already width * 4.
    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull()) {
        setError(QStringLiteral("Could not allocate the video frame image"));
        return;
    }

    for (int y = 0; y < height; ++y) {
        std::memcpy(image.scanLine(y), data.data() + size_t(y) * line_bytes, line_bytes);
    }

    // libass' blender writes 0 into the alpha byte of the pixels it touches,
    // while the preview is always opaque; force alpha back to 255. In
    // Format_ARGB32 the alpha channel is the high byte of each QRgb.
    auto *pixels = reinterpret_cast<QRgb *>(image.bits());
    const int pixel_count = width * height;
    for (int i = 0; i < pixel_count; ++i)
        pixels[i] |= 0xFF000000u;

    if (bridge_->FrameFlipped())
        image = image.mirrored(false, true);

    emit frameReady(image);
}

void VideoViewModel::setError(const QString &message) {
    if (last_error_ == message)
        return;
    last_error_ = message;
    LOG_E("qt/video") << message.toStdString();
    emit lastErrorChanged();
}

void VideoViewModel::resetError() {
    if (last_error_.isEmpty())
        return;
    last_error_.clear();
    emit lastErrorChanged();
}

void VideoViewModel::stopPlayback() {
    if (!playing_)
        return;

    playing_ = false;
    playback_timer_.stop();
    // A synced audio playback belongs to this transport: stop it as well so the
    // sound never keeps going while the picture is paused/stopped.
    if (audio_sync_ && audio_vm_)
        audio_vm_->stop();
    audio_sync_ = false;
    emit isPlayingChanged();
}

void VideoViewModel::onPlaybackTick() {
    if (!playing_ || !hasVideo())
        return;

    int target_ms = 0;

    if (audio_sync_ && audio_vm_) {
        // The audio player is the master clock. If it stopped on its own (end of
        // the stream, Play Line, Close Audio) the video stops where it is.
        if (!audio_vm_->isPlaying()) {
            stopPlayback();
            return;
        }
        target_ms = audio_vm_->playbackTimeMs();
    } else {
        // Wall-clock driven: a late timer tick skips a frame instead of slowing
        // the video down.
        target_ms = playback_start_ms_ + int(playback_elapsed_.elapsed());
    }

    const int last = frameCount() - 1;
    const int frame = bridge_->FrameAtTime(target_ms);

    if (frame >= last) {
        setCurrentFrame(last, true);
        stopPlayback();
        return;
    }

    setCurrentFrame(frame, true);
}

void VideoViewModel::onAudioAvailabilityChanged() {
    // Close Audio while a synced playback is running: there is no clock left to
    // follow, so stop cleanly instead of switching clocks mid-playback.
    if (playing_ && audio_sync_ && (!audio_vm_ || !audio_vm_->hasAudio()))
        stopPlayback();
}

void VideoViewModel::onAudioLinePlaybackStarted() {
    // Play Line takes over the audio device: pause the video transport instead
    // of letting the two fight over it.
    if (playing_)
        stopPlayback();
}

int VideoViewModel::activeLineStartMs() const {
    if (!grid_model_)
        return -1;

    const AssDialogue *line = grid_model_->getActiveDialogue();
    if (!line)
        return -1;

    return static_cast<int>(line->Start);
}

void VideoViewModel::onFileCommitted(int, const AssDialogue *) {
    if (!hasVideo())
        return;
    scheduleSubtitleReload();
}

void VideoViewModel::onRestored() {
    if (!hasVideo())
        return;

    // Undo/redo swaps the whole AssFile and other listeners rebuild their
    // indices after us; defer one event loop iteration before touching it.
    QTimer::singleShot(0, this, [this] {
        if (hasVideo())
            reloadSubtitlesAndRedraw();
    });
}

void VideoViewModel::onActiveRowChanged() {
    if (!hasVideo() || !auto_seek_ || playing_)
        return;
    seekToActiveLine();
}

void VideoViewModel::scheduleSubtitleReload() {
    // EditBox commits once per typed character; do not delay pending visual edits.
    if (!visual_reload_pending_)
        subtitle_reload_timer_.start(100);
}

void VideoViewModel::scheduleVisualSubtitleRefresh() {
    if (!hasVideo() || visual_reload_pending_)
        return;
    visual_reload_pending_ = true;
    subtitle_reload_timer_.start(16);
}

void VideoViewModel::flushVisualSubtitleRefresh() {
    if (visual_reload_pending_)
        reloadSubtitlesAndRedraw();
}

QSize VideoViewModel::scriptResolution() const {
    const int x = file_ ? file_->GetScriptInfoAsInt("PlayResX") : 0;
    const int y = file_ ? file_->GetScriptInfoAsInt("PlayResY") : 0;
    return {x > 0 ? x : (videoWidth() > 0 ? videoWidth() : 384),
            y > 0 ? y : (videoHeight() > 0 ? videoHeight() : 288)};
}

void VideoViewModel::loadSubtitleTrack() {
    if (!file_)
        return;
    const bool missing_x = file_->GetScriptInfoAsInt("PlayResX") <= 0;
    const bool missing_y = file_->GetScriptInfoAsInt("PlayResY") <= 0;
    if (!missing_x && !missing_y) {
        bridge_->LoadSubtitles(file_);
        return;
    }
    // Keep the libass preview and overlay in the same script space without
    // writing missing headers into the editable document or its undo history.
    const auto res = scriptResolution();
    AssFile preview(*file_);
    if (missing_x)
        preview.SetScriptInfo("PlayResX", std::to_string(res.width()));
    if (missing_y)
        preview.SetScriptInfo("PlayResY", std::to_string(res.height()));
    bridge_->LoadSubtitles(&preview);
}

void VideoViewModel::reloadSubtitlesAndRedraw() {
    subtitle_reload_timer_.stop();
    visual_reload_pending_ = false;
    if (!hasVideo() || !file_)
        return;
    loadSubtitleTrack();
    renderCurrentFrame();
}
