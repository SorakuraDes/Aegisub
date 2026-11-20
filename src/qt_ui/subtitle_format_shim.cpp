// Provides the non-inline parts of SubtitleFormat that AssSubtitleFormat needs
// at link time, without linking subtitle_format.cpp (which pulls in wxWidgets
// dialogs, from_wx()/wxString and the entire family of sibling subtitle
// formats through LoadFormats()).
//
// Only AssSubtitleFormat is used by the Qt frontend (open/save .ass/.ssa), so
// the base class constructor plus the two trivial wildcard checks are enough.
//
// Keep this file free of Qt headers; it is compiled into the same static
// library as the other Aegisub app-layer sources.
#include "../subtitle_format.h"

#include <libaegisub/fs.h>

#include <algorithm>
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
