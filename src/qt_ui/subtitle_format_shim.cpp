// Provides the non-inline parts of SubtitleFormat that AssSubtitleFormat and
// SRTSubtitleFormat need at link time, without linking subtitle_format.cpp
// (which pulls in wxWidgets dialogs, from_wx()/wxString and the entire family
// of sibling subtitle formats through LoadFormats()).
//
// Only AssSubtitleFormat (.ass/.ssa) and SRTSubtitleFormat (.srt) are used by
// the Qt frontend, so the base class constructor, the two trivial wildcard
// checks and the four wx-free AssFile post-processing helpers the SRT writer
// calls are enough.
//
// Keep this file free of Qt headers; it is compiled into the same static
// library as the other Aegisub app-layer sources.
#include "../subtitle_format.h"

#include <libaegisub/fs.h>

#include <boost/algorithm/string/replace.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

SubtitleFormat::SubtitleFormat(std::string name)
: name(std::move(name))
{
}

bool SubtitleFormat::CanReadFile(agi::fs::path const &filename, std::string const &) const {
    auto wildcards = GetReadWildcards();
    return std::any_of(wildcards.begin(), wildcards.end(),
        [&](std::string const &ext) { return agi::fs::HasExtension(filename, ext); });
}

bool SubtitleFormat::CanWriteFile(agi::fs::path const &filename) const {
    auto wildcards = GetWriteWildcards();
    return std::any_of(wildcards.begin(), wildcards.end(),
        [&](std::string const &ext) { return agi::fs::HasExtension(filename, ext); });
}

// The base-class vtable also references CanSave (the derived ASS format overrides
// it, but the base vtable still needs a definition), so mirror it here instead of
// pulling in subtitle_format.cpp.
#include "../ass_file.h"
#include "../ass_dialogue.h"

bool SubtitleFormat::CanSave(const AssFile *subs) const {
    if (!subs->Attachments.empty())
        return false;

    auto def = boost::flyweight<std::string>("Default");
    for (auto const &line : subs->Events) {
        if (line.Style != def || line.GetStrippedText() != line.Text)
            return false;
    }

    return true;
}

// SRTSubtitleFormat::WriteFile() calls these four static post-processing
// helpers. They live in subtitle_format.cpp next to the wx-powered
// LoadFormats()/AskForFPS() which the Qt frontend must not link, so the
// implementations are mirrored here verbatim (they only touch AssFile).
void SubtitleFormat::ConvertNewlines(AssFile &file, std::string const &newline,
                                     bool mergeLineBreaks) {
    for (auto &current : file.Events) {
        std::string repl = current.Text;
        boost::replace_all(repl, "\\h", " ");
        boost::ireplace_all(repl, "\\n", newline);
        if (mergeLineBreaks) {
            std::string dbl(newline + newline);
            size_t pos = 0;
            while ((pos = repl.find(dbl, pos)) != std::string::npos)
                boost::replace_all(repl, dbl, newline);
        }
        current.Text = repl;
    }
}

void SubtitleFormat::StripComments(AssFile &file) {
    file.Events.remove_and_dispose_if([](AssDialogue const &diag) {
        return diag.Comment || diag.Text.get().empty();
    }, [](AssDialogue *e) { delete e; });
}

/// @brief Split and merge lines so there are no overlapping lines
///
/// Algorithm described at http://devel.aegisub.org/wiki/Technical/SplitMerge
void SubtitleFormat::RecombineOverlaps(AssFile &file) {
    auto cur = file.Events.begin();
    for (auto next = std::next(cur); next != file.Events.end(); cur = std::prev(next)) {
        if (next == file.Events.begin() || cur->End <= next->Start) {
            ++next;
            continue;
        }

        std::unique_ptr<AssDialogue> prevdlg(&*cur);
        std::unique_ptr<AssDialogue> curdlg(&*next);
        ++next;

        auto insert_line = [&](AssDialogue *newdlg) {
            file.Events.insert(std::find_if(next, file.Events.end(), [&](AssDialogue const &pos) {
                return pos.Start >= newdlg->Start;
            }), *newdlg);
        };

        // Is there an A part before the overlap?
        if (curdlg->Start > prevdlg->Start) {
            auto newdlg = new AssDialogue(*prevdlg);
            newdlg->Start = prevdlg->Start;
            newdlg->End = curdlg->Start;
            newdlg->Text = prevdlg->Text;
            insert_line(newdlg);
        }

        // Overlapping A+B part
        {
            auto newdlg = new AssDialogue(*prevdlg);
            newdlg->Start = curdlg->Start;
            newdlg->End = (prevdlg->End < curdlg->End) ? prevdlg->End : curdlg->End;
            // Put an ASS format hard linewrap between lines
            newdlg->Text = curdlg->Text.get() + "\\N" + prevdlg->Text.get();
            insert_line(newdlg);
        }

        // Is there an A part after the overlap?
        if (prevdlg->End > curdlg->End) {
            auto newdlg = new AssDialogue(*prevdlg);
            newdlg->Start = curdlg->End;
            newdlg->End = prevdlg->End;
            newdlg->Text = prevdlg->Text;
            insert_line(newdlg);
        }

        // Is there a B part after the overlap?
        if (curdlg->End > prevdlg->End) {
            auto newdlg = new AssDialogue(*prevdlg);
            newdlg->Start = prevdlg->End;
            newdlg->End = curdlg->End;
            newdlg->Text = curdlg->Text;
            insert_line(newdlg);
        }
    }
}

/// @brief Merge identical lines that follow each other
void SubtitleFormat::MergeIdentical(AssFile &file) {
    auto next = file.Events.begin();
    auto cur = next++;

    for (; next != file.Events.end(); cur = next++) {
        if (cur->End == next->Start && cur->Text == next->Text) {
            // Merge timing
            next->Start = std::min(next->Start, cur->Start);
            next->End = std::max(next->End, cur->End);

            // Remove duplicate line
            delete &*cur;
        }
    }
}
