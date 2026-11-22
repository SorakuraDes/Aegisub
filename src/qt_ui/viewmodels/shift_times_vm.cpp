// ass_file.h first: agi::signal has a member named `slots`, which Qt's macros
// would mangle if a Qt header were parsed first (see docs/qt6-frontend.md #2).
#include "../../ass_file.h"

#include "shift_times_vm.h"

#include "subtitle_grid_model.h"
#include "video_vm.h"

#include "../commit_bridge.h"

#include "../../ass_dialogue.h"

#include <libaegisub/ass/time.h>

#include <QRegularExpression>

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <vector>

ShiftTimesViewModel::ShiftTimesViewModel(AssFile *file, SubtitleGridModel *gridModel,
                                         VideoViewModel *videoVM, QObject *parent)
: QObject(parent)
, file_(file)
, grid_model_(gridModel)
, video_vm_(videoVM)
{
    // The frame mode is only offered while a video is open; auto-deselect it
    // when the user closes the video so the dialog never sits in a dead mode.
    if (video_vm_) {
        connect(video_vm_, &VideoViewModel::hasVideoChanged,
                this, &ShiftTimesViewModel::onVideoAvailabilityChanged);
    }
}

bool ShiftTimesViewModel::byFrames() const {
    return by_frames_;
}

void ShiftTimesViewModel::setByFrames(bool value) {
    if (by_frames_ == value)
        return;
    by_frames_ = value;
    emit byFramesChanged();
}

bool ShiftTimesViewModel::framesAvailable() const {
    return video_vm_ && video_vm_->hasVideo();
}

QString ShiftTimesViewModel::shiftTime() const {
    return shift_time_;
}

void ShiftTimesViewModel::setShiftTime(const QString &value) {
    if (shift_time_ == value)
        return;
    shift_time_ = value;
    emit shiftTimeChanged();
}

int ShiftTimesViewModel::shiftFrames() const {
    return shift_frames_;
}

void ShiftTimesViewModel::setShiftFrames(int value) {
    if (shift_frames_ == value)
        return;
    shift_frames_ = value;
    emit shiftFramesChanged();
}

bool ShiftTimesViewModel::backward() const {
    return backward_;
}

void ShiftTimesViewModel::setBackward(bool value) {
    if (backward_ == value)
        return;
    backward_ = value;
    emit backwardChanged();
}

int ShiftTimesViewModel::affectScope() const {
    return affect_scope_;
}

void ShiftTimesViewModel::setAffectScope(int value) {
    if (affect_scope_ == value)
        return;
    affect_scope_ = value;
    emit affectScopeChanged();
}

int ShiftTimesViewModel::timeTarget() const {
    return time_target_;
}

void ShiftTimesViewModel::setTimeTarget(int value) {
    if (time_target_ == value)
        return;
    time_target_ = value;
    emit timeTargetChanged();
}

QString ShiftTimesViewModel::statusMessage() const {
    return status_;
}

int ShiftTimesViewModel::applyShift() {
    if (!file_ || !grid_model_) {
        setStatus(QStringLiteral("No lines changed"));
        return 0;
    }

    // ---- Validate the shift amount before touching any line ----------------
    if (by_frames_ && !framesAvailable()) {
        setStatus(QStringLiteral("Frame mode requires an open video"));
        return 0;
    }

    int shift_ms = 0;
    int signed_frames = 0;
    if (by_frames_) {
        signed_frames = backward_ ? -shift_frames_ : shift_frames_;
        if (signed_frames == 0) {
            setStatus(QStringLiteral("No lines changed"));
            return 0;
        }
    } else {
        if (!parseShiftTime(&shift_ms)) {
            setStatus(QStringLiteral("Invalid time"));
            return 0;
        }
        if (backward_)
            shift_ms = -shift_ms;
        if (shift_ms == 0) {
            setStatus(QStringLiteral("No lines changed"));
            return 0;
        }
    }

    // ---- Collect the affected lines in file order --------------------------
    // The grid model owns the row order (a vector over AssFile::Events), so
    // both the "all rows" and the "selection onward" scopes walk it instead of
    // the intrusive list.
    std::vector<AssDialogue *> lines;
    const int row_count = grid_model_->rowCount();
    switch (affect_scope_) {
        case 1: {
            // Selected rows only.
            for (int row : grid_model_->sortedSelectedRows()) {
                if (AssDialogue *line = grid_model_->dialogueAt(row))
                    lines.push_back(line);
            }
            break;
        }
        case 2: {
            // wx mode 2: from the first selected row through the end of the
            // file, including the rows that are not selected themselves.
            int first = -1;
            const QList<int> selected = grid_model_->sortedSelectedRows();
            if (!selected.isEmpty())
                first = selected.first();
            else
                first = grid_model_->activeRow();
            if (first < 0)
                first = 0;
            for (int row = first; row < row_count; ++row) {
                if (AssDialogue *line = grid_model_->dialogueAt(row))
                    lines.push_back(line);
            }
            break;
        }
        default: {
            for (int row = 0; row < row_count; ++row) {
                if (AssDialogue *line = grid_model_->dialogueAt(row))
                    lines.push_back(line);
            }
            break;
        }
    }

    if (lines.empty()) {
        setStatus(QStringLiteral("No lines changed"));
        return 0;
    }

    // ---- Shift Start and/or End of every line -----------------------------
    const bool shift_start = time_target_ != 2;
    const bool shift_end = time_target_ != 1;

    // Both the time arithmetic and the frame arithmetic can leave the int
    // range when the input is extreme (agi::Time tops out at 596h), so the
    // intermediate values are widened and clamped instead of overflowing.
    const long long int_min = static_cast<long long>(std::numeric_limits<int>::min());
    const long long int_max = static_cast<long long>(std::numeric_limits<int>::max());
    auto shift_one = [&](int old, bool end) -> int {
        if (by_frames_) {
            const int frame = video_vm_->frameAtTimeMs(old, end);
            const long long target =
                std::clamp(static_cast<long long>(frame) + signed_frames, int_min, int_max);
            return video_vm_->timeAtFrameMs(static_cast<int>(target), end);
        }
        const long long shifted = static_cast<long long>(old) + shift_ms;
        return static_cast<int>(std::clamp(shifted, int_min, int_max));
    };

    int changed = 0;
    for (AssDialogue *line : lines) {
        const int old_start = static_cast<int>(line->Start);
        const int old_end = static_cast<int>(line->End);

        int new_start = shift_start ? shift_one(old_start, false) : old_start;
        int new_end = shift_end ? shift_one(old_end, true) : old_end;

        // Times are never negative, and End must not move before Start even
        // when only one of the two fields is being shifted.
        if (shift_start)
            new_start = std::max(0, new_start);
        if (shift_start && shift_end) {
            new_end = std::max(new_start, new_end);
        } else if (shift_start) {
            new_start = std::min(new_start, old_end);
        } else {
            new_end = std::max(new_end, old_start);
        }

        if (new_start == old_start && new_end == old_end)
            continue;

        line->Start = new_start;
        line->End = new_end;
        ++changed;
    }

    if (changed == 0) {
        setStatus(QStringLiteral("No lines changed"));
        return 0;
    }

    // Exactly one commit for the whole batch: a single Ctrl+Z reverts every
    // shifted line, and the grid / edit box / video / audio refresh through the
    // normal AssFile::Commit bus (single_line is null because this is a batch).
    aegisub_qt::commit(file_, "shifting", AssFile::COMMIT_DIAG_TIME, -1, nullptr);

    setStatus(QStringLiteral("Shifted %1 line(s)").arg(changed));
    return changed;
}

void ShiftTimesViewModel::onVideoAvailabilityChanged() {
    emit framesAvailableChanged();
    if (!framesAvailable() && by_frames_) {
        by_frames_ = false;
        emit byFramesChanged();
    }
}

void ShiftTimesViewModel::setStatus(const QString &message) {
    if (status_ == message)
        return;
    status_ = message;
    emit statusMessageChanged();
}

bool ShiftTimesViewModel::parseShiftTime(int *ms) const {
    const QString text = shift_time_.trimmed();
    if (text.isEmpty())
        return false;

    // agi::Time's string constructor never throws: it ignores junk characters
    // and silently resolves nonsense to 0, so reject anything that does not
    // look like a time before handing it over.
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9]+(?::[0-9]+){0,2}(?:[.,][0-9]+)?$"));
    if (!pattern.match(text).hasMatch())
        return false;

    try {
        *ms = static_cast<int>(agi::Time(text.toStdString()));
    } catch (const std::exception &) {
        return false;
    }

    return true;
}
