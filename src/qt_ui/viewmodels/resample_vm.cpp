// The ASS headers must precede Qt's signals/slots macros.
#include "../../ass_file.h"
#include "../../ass_dialogue.h"
#include "../../ass_style.h"

#include "../commit_bridge.h"
#include "resample_vm.h"

#include "../ass_tag_utils.h"
#include "video_vm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace {

int scaleMargin(int margin, double factor) {
    return static_cast<int>(std::clamp(std::round(margin * factor), 0.0,
                                      double(std::numeric_limits<int>::max())));
}

} // namespace

ResampleViewModel::ResampleViewModel(AssFile *file, VideoViewModel *videoVM, QObject *parent)
: QObject(parent)
, file_(file)
, video_vm_(videoVM) {
    if (video_vm_)
        connect(video_vm_, &VideoViewModel::videoInfoChanged,
                this, &ResampleViewModel::videoDimensionsChanged);
}

int ResampleViewModel::videoWidth() const {
    return video_vm_ && video_vm_->hasVideo() ? video_vm_->videoWidth() : 0;
}

int ResampleViewModel::videoHeight() const {
    return video_vm_ && video_vm_->hasVideo() ? video_vm_->videoHeight() : 0;
}

void ResampleViewModel::refreshSource() {
    int width = 384, height = 288;
    if (file_)
        file_->GetResolution(width, height);
    if (width == source_width_ && height == source_height_)
        return;
    source_width_ = width;
    source_height_ = height;
    emit sourceChanged();
}

bool ResampleViewModel::apply(const QString &width, const QString &height) {
    bool validWidth, validHeight;
    const int targetWidth = width.toInt(&validWidth);
    const int targetHeight = height.toInt(&validHeight);
    if (!file_ || !validWidth || !validHeight || targetWidth < 1 || targetHeight < 1)
        return false;

    refreshSource();
    if (source_width_ < 1 || source_height_ < 1)
        return false;
    if (targetWidth == file_->GetScriptInfoAsInt("PlayResX") &&
        targetHeight == file_->GetScriptInfoAsInt("PlayResY"))
        return true;

    const double scaleX = double(targetWidth) / source_width_;
    const double scaleY = double(targetHeight) / source_height_;
    if (scaleX != 1 || scaleY != 1) {
        for (auto &style : file_->Styles) {
            style.fontsize = std::round(style.fontsize * scaleY);
            style.outline_w *= scaleY;
            style.shadow_w *= scaleY;
            for (size_t i = 0; i < style.Margin.size(); ++i)
                style.Margin[i] = scaleMargin(style.Margin[i], i < 2 ? scaleX : scaleY);
            style.UpdateData();
        }
        for (auto &line : file_->Events) {
            const auto &effect = line.Effect.get();
            if (line.Comment && (effect.rfind("template", 0) == 0 || effect.rfind("code", 0) == 0))
                continue; // Keep automation source intact, as in the wx implementation.
            for (size_t i = 0; i < line.Margin.size(); ++i)
                line.Margin[i] = scaleMargin(line.Margin[i], i < 2 ? scaleX : scaleY);
            const auto &text = line.Text.get();
            auto replacement = aegisub_qt::resampleLineTags(text, scaleX, scaleY);
            if (replacement != text)
                line.Text = std::move(replacement);
        }
    }

    file_->SetScriptInfo("PlayResX", std::to_string(targetWidth));
    file_->SetScriptInfo("PlayResY", std::to_string(targetHeight));
    aegisub_qt::commit(file_, "resolution resampling",
        AssFile::COMMIT_SCRIPTINFO | AssFile::COMMIT_STYLES | AssFile::COMMIT_DIAG_FULL);
    refreshSource();
    return true;
}
