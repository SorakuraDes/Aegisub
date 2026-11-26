// ASS serialization adapted from subtitle_format_ass.cpp:
// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// SRT serialization adapted from subtitle_format_srt.cpp:
// Copyright (c) 2006, Rodrigo Braz Monteiro
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "ass_attachment.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "options.h"
#include "subtitle_format.h"
#include "version.h"

#include <libaegisub/charset_conv.h>

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aegisub_qt::automation {
namespace export_writer_detail {

#ifdef _WIN32
inline constexpr char newline[] = "\r\n";
#else
inline constexpr char newline[] = "\n";
#endif

struct Cancelled {};

class Writer {
    QFile file;
    const std::atomic_bool &cancelled;
    std::unique_ptr<agi::charset::IconvWrapper> converter;
    std::string encodedNewline = newline;

    [[noreturn]] void fail(const QString &operation) const {
        throw std::runtime_error(QStringLiteral("%1 temporary subtitle file %2: %3")
                                     .arg(operation, file.fileName(), file.errorString()).toStdString());
    }

    void writeBytes(std::string_view bytes) {
        constexpr std::size_t chunkSize = 64 * 1024;
        while (!bytes.empty()) {
            checkCancelled();
            const auto length = static_cast<qint64>((std::min)(chunkSize, bytes.size()));
            const auto written = file.write(bytes.data(), length);
            if (written < 0) fail(QStringLiteral("Could not write"));
            if (written == 0) throw std::runtime_error("Could not write temporary subtitle file: no progress");
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
    }

public:
    Writer(const QString &path, const QString &encoding, const std::atomic_bool &cancelled)
        : file(path), cancelled(cancelled) {
        checkCancelled();
        const auto charset = encoding.isEmpty() ? OPT_GET("App/Save Charset")->GetString()
                                                : encoding.toStdString();
        if (charset != "utf-8" && charset != "UTF-8") {
            converter = std::make_unique<agi::charset::IconvWrapper>("utf-8", charset.c_str(), true);
            // Match the native writer's order: convert its line terminator before its BOM.
            encodedNewline = converter->Convert(encodedNewline);
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) fail(QStringLiteral("Could not open"));
        try {
            line("\xEF\xBB\xBF", false);
        } catch (const agi::charset::ConversionFailure &) {
            // A legacy encoding which cannot represent the BOM does not need one.
        }
    }

    void checkCancelled() const {
        if (cancelled.load()) throw Cancelled{};
    }

    void line(std::string_view text, bool addNewline = true) {
        checkCancelled();
        if (converter) writeBytes(converter->Convert(text.data(), text.size()));
        else writeBytes(text);
        if (addNewline) writeBytes(encodedNewline);
    }

    void finish() {
        checkCancelled();
        if (!file.flush()) fail(QStringLiteral("Could not flush"));
        file.close();
        if (file.error() != QFileDevice::NoError) fail(QStringLiteral("Could not close"));
        checkCancelled();
    }
};

inline void writeAss(const AssFile &subs, Writer &writer) {
    writer.line("[Script Info]");
    writer.line(std::string("; Script generated by Aegisub ") + GetAegisubLongVersionString());
    writer.line("; http://www.aegisub.org/");

    auto group = AssEntryGroup::INFO;
    const auto writeEntries = [&](const auto &entries) {
        for (const auto &entry : entries) {
            writer.checkCancelled();
            if (entry.Group() != group) {
                group = entry.Group();
                writer.line("");
                writer.line(entry.GroupHeader());
                // The native writer encodes each Format line with its terminator in one conversion.
                if (group == AssEntryGroup::STYLE)
                    writer.line(std::string("Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                                            "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
                                            "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                                            "Alignment, MarginL, MarginR, MarginV, Encoding") + newline, false);
                if (group == AssEntryGroup::DIALOGUE)
                    writer.line(std::string("Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
                                            "MarginV, Effect, Text") + newline, false);
            }
            writer.line(entry.GetEntryData());
        }
    };
    // ExportFile, unlike WriteFile, excludes the project properties and extradata.
    writeEntries(subs.Info);
    writeEntries(subs.Styles);
    writeEntries(subs.Attachments);
    writeEntries(subs.Events);
}

// SRTSubtitleFormat::ConvertTags is private. Match its b/i/s/u toggle and closing order.
inline std::string srtText(const AssDialogue &dialogue, const Writer &writer) {
    struct TagState { char tag; bool active = false; };
    TagState states[] = {{'b'}, {'i'}, {'s'}, {'u'}};
    std::string text;
    const auto appendTag = [&](char tag, bool open) {
        text += open ? "<" : "</";
        text += tag;
        text += '>';
    };
    for (const auto &block : dialogue.ParseTags()) {
        writer.checkCancelled();
        switch (block->GetType()) {
        case AssBlockType::OVERRIDE:
            for (const auto &tag : static_cast<const AssDialogueBlockOverride &>(*block).Tags) {
                if (!tag.IsValid() || tag.Name.size() != 2) continue;
                for (auto &state : states) {
                    if (state.tag != tag.Name[1]) continue;
                    const bool active = tag.Params[0].Get(false);
                    if (active != state.active) appendTag(state.tag, active);
                    state.active = active;
                }
            }
            break;
        case AssBlockType::PLAIN:
            text += block->GetText();
            break;
        case AssBlockType::DRAWING:
        case AssBlockType::COMMENT:
            break;
        }
    }
    for (const auto &state : states)
        if (state.active) appendTag(state.tag, false);
    return text;
}

inline void writeSrt(const AssFile &subs, Writer &writer) {
    writer.checkCancelled();
    AssFile copy(subs);
    writer.checkCancelled();
    copy.Sort();
    writer.checkCancelled();
    SubtitleFormat::StripComments(copy);
    // The two intrusive-list transforms assume there is an event to start with.
    if (copy.Events.empty()) return;
    writer.checkCancelled();
    SubtitleFormat::RecombineOverlaps(copy);
    writer.checkCancelled();
    SubtitleFormat::MergeIdentical(copy);
    writer.checkCancelled();
    SubtitleFormat::ConvertNewlines(copy, newline, false);

    std::size_t index = 0;
    for (const auto &event : copy.Events) {
        writer.line(std::to_string(++index));
        writer.line(event.Start.GetSrtFormatted() + " --> " + event.End.GetSrtFormatted());
        writer.line(srtText(event, writer));
        writer.line("");
    }
}

} // namespace export_writer_detail

// The caller owns the temporary directory and publishes the file only on success.
// Returns false on cancellation; throws on unsupported format, conversion or I/O failure.
inline bool writeExportFile(const AssFile &subs, const QString &temporaryPath, const QString &encoding,
                            const std::atomic_bool &cancelled) {
    using namespace export_writer_detail;
    if (cancelled.load()) return false;
    const auto name = QFileInfo(temporaryPath).fileName();
    const bool ass = name.endsWith(QStringLiteral(".ass"), Qt::CaseInsensitive);
    if (!ass && !name.endsWith(QStringLiteral(".srt"), Qt::CaseInsensitive)) {
        // The native ASS format reads SSA but its writable wildcard is ASS only.
        if (name.endsWith(QStringLiteral(".ssa"), Qt::CaseInsensitive))
            throw std::runtime_error("SSA subtitle export is not supported");
        throw std::runtime_error("Unknown subtitle export format");
    }
    try {
        Writer writer(temporaryPath, encoding, cancelled);
        if (ass) writeAss(subs, writer);
        else writeSrt(subs, writer);
        writer.finish();
        return true;
    } catch (const Cancelled &) {
        return false;
    }
}

} // namespace aegisub_qt::automation
