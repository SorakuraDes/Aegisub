#include "../../ass_file.h" // agi::signal before Qt headers
#include "../../ass_dialogue.h"
#include "visual_tools_vm.h"

#include "../ass_tag_utils.h"
#include "../commit_bridge.h"
#include "subtitle_grid_model.h"
#include "undo_manager.h"
#include "video_vm.h"

#include <QScopedValueRollback>
#include <cmath>
#include <utility>

VisualToolsViewModel::VisualToolsViewModel(AssFile *file, SubtitleGridModel *gridModel,
                                           VideoViewModel *videoVM, UndoManager *undoManager)
: file_(file), grid_model_(gridModel), video_vm_(videoVM) {
    commit_connection_ = file_->AddCommitListener(&VisualToolsViewModel::onFileCommitted, this);
    connect(grid_model_, &SubtitleGridModel::activeRowChanged, this, &VisualToolsViewModel::syncSelection);
    connect(grid_model_, &QAbstractItemModel::modelReset, this, &VisualToolsViewModel::syncSelection);
    connect(undoManager, &UndoManager::restored, this, &VisualToolsViewModel::syncSelection);
    connect(video_vm_, &VideoViewModel::videoInfoChanged, this, &VisualToolsViewModel::handleDocumentReload);
    refresh();
}

void VisualToolsViewModel::setActiveTool(int tool) {
    if (tool < Standard || tool > RectClip || tool == active_tool_)
        return;
    endDrag();
    active_tool_ = tool;
    emit activeToolChanged();
}

AssDialogue *VisualToolsViewModel::activeLine() const {
    return grid_model_->getActiveDialogue();
}

void VisualToolsViewModel::syncSelection() {
    endDrag();
    refresh();
}

void VisualToolsViewModel::handleDocumentReload() {
    clearHoverCoord();
    syncSelection();
}

void VisualToolsViewModel::refresh() {
    const auto res = video_vm_->scriptResolution();
    play_res_x_ = res.width();
    play_res_y_ = res.height();
    const auto *line = activeLine();
    has_active_line_ = line != nullptr;
    pos_x_ = pos_y_ = 0;
    clip_x1_ = clip_y1_ = 0;
    clip_x2_ = play_res_x_;
    clip_y2_ = play_res_y_;
    has_explicit_pos_ = has_rect_clip_ = false;
    if (line) {
        if (!aegisub_qt::parsePos(line->Text.get(), &pos_x_, &pos_y_, &has_explicit_pos_)) {
            const auto *style = file_->GetStyle(line->Style);
            if (!style)
                style = file_->GetStyle("Default");
            const auto [x, y] = aegisub_qt::defaultAnchorPos(*line, style, play_res_x_, play_res_y_);
            pos_x_ = x;
            pos_y_ = y;
        }
        has_rect_clip_ = aegisub_qt::parseRectClip(line->Text.get(), &clip_x1_, &clip_y1_, &clip_x2_, &clip_y2_);
    }
    emit visualStateChanged();
}

void VisualToolsViewModel::onFileCommitted(int type, const AssDialogue *line) {
    // The grid sees core commits after us. Do not touch its old row pointers
    // on a structural commit; its modelReset will rebuild the overlay.
    if (type == AssFile::COMMIT_NEW || (type & (AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_ORDER)))
        return;
    if (!(type & (AssFile::COMMIT_DIAG_TEXT | AssFile::COMMIT_DIAG_META | AssFile::COMMIT_SCRIPTINFO | AssFile::COMMIT_STYLES)))
        return;
    if (line && line != activeLine() && !(type & (AssFile::COMMIT_SCRIPTINFO | AssFile::COMMIT_STYLES)))
        return;
    if (!committing_)
        endDrag();
    if (type & AssFile::COMMIT_SCRIPTINFO)
        clearHoverCoord();
    refresh();
}

bool VisualToolsViewModel::beginDrag() {
    if (dragging_ || active_tool_ == Standard || !video_vm_->hasVideo() || !activeLine())
        return false;
    drag_commit_id_ = -1;
    dragging_ = true;
    return true;
}

void VisualToolsViewModel::updatePos(double sx, double sy) {
    if (dragging_ && active_tool_ == DragPos && std::isfinite(sx) && std::isfinite(sy)) {
        if (auto *line = activeLine())
            commitText(line, aegisub_qt::setPosTag(line->Text.get(), sx, sy), "position subtitle", true);
    }
}

void VisualToolsViewModel::updateRectClip(int x1, int y1, int x2, int y2) {
    if (dragging_ && active_tool_ == RectClip) {
        if (auto *line = activeLine())
            commitText(line, aegisub_qt::setRectClipTag(line->Text.get(), x1, y1, x2, y2), "set rectangular clip", true);
    }
}

void VisualToolsViewModel::endDrag() {
    if (!dragging_)
        return;
    dragging_ = false;
    drag_commit_id_ = -1;
    video_vm_->flushVisualSubtitleRefresh();
    emit dragEnded();
}

void VisualToolsViewModel::clearRectClip() {
    endDrag();
    if (auto *line = activeLine())
        commitText(line, aegisub_qt::removeRectClipTag(line->Text.get()), "clear rectangular clip", false);
}

void VisualToolsViewModel::commitText(AssDialogue *line, std::string text, const char *description, bool amend) {
    if (line->Text == text)
        return;
    line->Text = std::move(text);
    const QScopedValueRollback guard(committing_, true);
    // The bridge returns the id written back through AssFile's commit bus.
    const int id = aegisub_qt::commit(file_, description, AssFile::COMMIT_DIAG_TEXT,
                                      amend ? drag_commit_id_ : -1, line);
    if (amend && dragging_)
        drag_commit_id_ = id;
    video_vm_->scheduleVisualSubtitleRefresh();
    if (!amend)
        video_vm_->flushVisualSubtitleRefresh();
}

void VisualToolsViewModel::setHoverCoord(double sx, double sy) {
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx < 0 || sy < 0 || sx > play_res_x_ || sy > play_res_y_) {
        clearHoverCoord();
        return;
    }
    auto text = QStringLiteral("%1, %2").arg(std::llround(sx)).arg(std::llround(sy));
    if (hover_coord_text_ != text) {
        hover_coord_text_ = std::move(text);
        emit hoverCoordTextChanged();
    }
}

void VisualToolsViewModel::clearHoverCoord() {
    if (!hover_coord_text_.isEmpty()) {
        hover_coord_text_.clear();
        emit hoverCoordTextChanged();
    }
}
