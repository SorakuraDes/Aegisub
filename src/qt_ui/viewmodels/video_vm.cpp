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

#include "../video_bridge.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>

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
    playback_timer_.setTimerType(Qt::PreciseTimer);
    connect(&playback_timer_, &QTimer::timeout, this, &VideoViewModel::onPlaybackTick);

    subtitle_reload_timer_.setSingleShot(true);
    subtitle_reload_timer_.setInterval(100);
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
int VideoViewModel::videoHeight() const { return bridge_->Height(); }

int VideoViewModel::currentTimeMs() const {
    if (!hasVideo() || current_frame_ < 0)
        return 0;
    return bridge_->TimeAtFrame(current_frame_);
}

QString VideoViewModel::currentTimeText() const {
    return QString::fromStdString(agi::Time(currentTimeMs()).GetAssFormatted());
}

void VideoViewModel::setAutoSeekToActiveLine(bool enabled) {
    if (auto_seek_ == enabled)
        return;

    auto_seek_ = enabled;
    emit autoSeekToActiveLineChanged();

    if (enabled && hasVideo() && !playing_)
        seekToActiveLine();
}

bool VideoViewModel::openVideo(const QUrl &url) {
    if (url.isLocalFile())
        return openLocalPath(url.toLocalFile());
    return openLocalPath(url.toString());
}

bool VideoViewModel::openLocalPath(const QString &path) {
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

    std::string error;
    if (!bridge_->OpenFile(path.toUtf8().toStdString(), &error)) {
        setError(QString::fromStdString(error));
        return false;
    }

    file_path_ = info.absoluteFilePath();
    onVideoOpened(info.fileName());
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
        resolved_width = 1280;
        resolved_height = 720;
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
    return true;
}

void VideoViewModel::closeVideo() {
    const bool was_playing = playing_;
    playing_ = false;
    playback_timer_.stop();
    subtitle_reload_timer_.stop();

    bridge_->Close();

    video_name_.clear();
    file_path_.clear();
    current_frame_ = -1;

    emit hasVideoChanged();
    emit videoNameChanged();
    emit videoInfoChanged();
    emit currentFrameChanged();
    if (was_playing)
        emit isPlayingChanged();
    emit frameReady(QImage());
}

void VideoViewModel::seekFrame(int frame) {
    if (!hasVideo())
        return;

    const int last = frameCount() - 1;
    const int target = std::clamp(frame, 0, last);

    if (playing_) {
        // Keep playback running from the new position.
        playback_start_ms_ = bridge_->TimeAtFrame(target);
        playback_elapsed_.restart();
    }

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

void VideoViewModel::seekToActiveLine() {
    if (!hasVideo())
        return;

    const int start_ms = activeLineStartMs();
    if (start_ms < 0)
        return;

    // START semantics: land on the first frame on which the line is visible.
    seekFrame(bridge_->FrameAtTimeStart(start_ms));
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

    playing_ = true;
    playback_start_ms_ = currentTimeMs();
    playback_elapsed_.start();

    const double frames_per_second = fps() > 0.0 ? fps() : 24.0;
    playback_timer_.start(std::max(1, int(1000.0 / frames_per_second + 0.5)));
    emit isPlayingChanged();
}

void VideoViewModel::handleDocumentReload() {
    if (!hasVideo())
        return;
    reloadSubtitlesAndRedraw();
}

void VideoViewModel::onVideoOpened(const QString &name) {
    video_name_ = name;
    resetError();

    playing_ = false;
    playback_timer_.stop();
    current_frame_ = -1;

    if (file_)
        bridge_->LoadSubtitles(file_);

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
    emit currentFrameChanged();

    if (redraw)
        renderCurrentFrame();
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
    emit isPlayingChanged();
}

void VideoViewModel::onPlaybackTick() {
    if (!playing_ || !hasVideo())
        return;

    // Wall-clock driven: a late timer tick skips a frame instead of slowing the
    // video down.
    const int target_ms = playback_start_ms_ + int(playback_elapsed_.elapsed());
    const int last = frameCount() - 1;
    const int frame = bridge_->FrameAtTime(target_ms);

    if (frame >= last) {
        setCurrentFrame(last, true);
        stopPlayback();
        return;
    }

    setCurrentFrame(frame, true);
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
    // Debounce: EditBox commits once per typed character.
    subtitle_reload_timer_.start();
}

void VideoViewModel::reloadSubtitlesAndRedraw() {
    if (!hasVideo() || !file_)
        return;

    bridge_->LoadSubtitles(file_);
    renderCurrentFrame();
}
