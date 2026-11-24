// Copyright (c) 2008-2009, Karl Blomster
// All rights reserved.
//
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
//
// Aegisub Project http://www.aegisub.org/

/// @file ffmpegsource_common_qt.cpp
/// @brief Qt copy of src/ffmpegsource_common.cpp with the wxWidgets track
///        selection dialog replaced by an injectable C++ callback (Stage 11B)
/// @ingroup video_input audio_input ffms
///
/// The original file is compiled into the wxWidgets frontend and must not be
/// modified. Its only GUI dependency is
/// `wxGetSingleChoiceIndex()` in AskForTrackSelection(), which would pop a wx
/// dialog from the Qt process (where no wxApp exists). This copy is compiled
/// into `av_qt_core` instead of the original: everything is identical except
/// that AskForTrackSelection() asks aegisub_qt::TrackSelector for the track id
/// (installed by the bridges) and otherwise selects the first track, so the Qt
/// frontend never shows a wx dialog.

#ifdef WITH_FFMS2
#include "ffmpegsource_common_qt.h"

#include "ffmpegsource_common.h"

#include "options.h"
#include "utils.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/crc.hpp>
#include <boost/filesystem/path.hpp>

#include <ffms.h>

#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace aegisub_qt {
namespace {

std::mutex g_selector_mutex;
TrackSelector g_selector;

TrackSelector CopySelector() {
    std::lock_guard<std::mutex> lock(g_selector_mutex);
    return g_selector;
}

/// UTF-8 -> native path (boost::filesystem::path on Windows is UTF-16).
agi::fs::path pathFromUtf8(std::string const &utf8) {
#ifdef _WIN32
    if (utf8.empty())
        return agi::fs::path();
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring wide(size_t(length > 0 ? length : 0), L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), length);
    return agi::fs::path(wide);
#else
    return agi::fs::path(utf8);
#endif
}

FFMS_TrackType ToFFMSType(MediaKind kind) {
    return kind == MediaKind::Video ? FFMS_TYPE_VIDEO : FFMS_TYPE_AUDIO;
}

} // namespace

void SetTrackSelector(TrackSelector selector) {
    std::lock_guard<std::mutex> lock(g_selector_mutex);
    g_selector = std::move(selector);
}

void ClearTrackSelector() { SetTrackSelector(TrackSelector()); }

bool ProbeTracks(const std::string &utf8_path, MediaKind kind,
                 std::vector<MediaTrackInfo> *tracks, std::string *error) {
    if (tracks)
        tracks->clear();

    char error_buffer[1024];
    FFMS_ErrorInfo error_info;
    error_info.Buffer = error_buffer;
    error_info.BufferSize = sizeof(error_buffer);
    error_info.ErrorType = FFMS_ERROR_SUCCESS;
    error_info.SubType = FFMS_ERROR_SUCCESS;

    FFMS_Indexer *indexer =
        FFMS_CreateIndexer(pathFromUtf8(utf8_path).string().c_str(), &error_info);
    if (!indexer) {
        if (error)
            *error = error_buffer;
        return false;
    }

    // GetTracksOfType() only reads the indexer's stream list; the progress
    // runner is not touched, so a provider with no runner is enough here.
    FFmpegSourceProvider probe(nullptr);
    const std::map<int, std::string> list = probe.GetTracksOfType(indexer, ToFFMSType(kind));

    FFMS_CancelIndexing(indexer);

    if (tracks) {
        tracks->reserve(list.size());
        for (auto const &track : list)
            tracks->push_back(MediaTrackInfo{track.first, track.second});
    }
    return true;
}

} // namespace aegisub_qt

// ---- Qt copy of FFmpegSourceProvider (see the file comment above) ---------

FFmpegSourceProvider::FFmpegSourceProvider(agi::BackgroundRunner *br)
: br(br)
{
    FFMS_Init(0, 0);
}

/// @brief Does indexing of a source file
/// @param Indexer      A pointer to the indexer object representing the file to be indexed
/// @param CacheName    The filename of the output index file
/// @param Track        A binary mask of the track numbers to index
FFMS_Index *FFmpegSourceProvider::DoIndexing(FFMS_Indexer *Indexer,
                                             agi::fs::path const& CacheName,
                                             TrackSelection Track,
                                             FFMS_IndexErrorHandling IndexEH) {
    char FFMSErrMsg[1024];
    FFMS_ErrorInfo ErrInfo;
    ErrInfo.Buffer = FFMSErrMsg;
    ErrInfo.BufferSize = sizeof(FFMSErrMsg);
    ErrInfo.ErrorType = FFMS_ERROR_SUCCESS;
    ErrInfo.SubType = FFMS_ERROR_SUCCESS;

    // index all audio tracks
    FFMS_Index *Index;
    br->Run([&](agi::ProgressSink *ps) {
        ps->SetTitle("Indexing");
        ps->SetMessage("Reading timecodes and frame/sample data");
        TIndexCallback callback = [](int64_t Current, int64_t Total, void *Private) -> int {
            auto ps = static_cast<agi::ProgressSink *>(Private);
            ps->SetProgress(Current, Total);
            return ps->IsCancelled();
        };
        if (Track == TrackSelection::All)
            FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 0);
        else if (Track != TrackSelection::None)
            FFMS_TrackIndexSettings(Indexer, static_cast<int>(Track), 1, 0);
        FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_VIDEO, 1, 0);
        FFMS_SetProgressCallback(Indexer, callback, ps);
        Index = FFMS_DoIndexing2(Indexer, IndexEH, &ErrInfo);
    });

    if (Index == nullptr)
        throw agi::EnvironmentError(std::string("Failed to index: ") + ErrInfo.Buffer);

    // write index to disk for later use
    FFMS_WriteIndex(CacheName.string().c_str(), Index, &ErrInfo);

    return Index;
}

/// @brief Finds all tracks of the given type and return their track numbers and respective codec names
/// @param Indexer  The indexer object representing the source file
/// @param Type     The track type to look for
/// @return         Returns a std::map with the track numbers as keys and the codec names as values.
std::map<int, std::string> FFmpegSourceProvider::GetTracksOfType(FFMS_Indexer *Indexer, FFMS_TrackType Type) {
    std::map<int, std::string> TrackList;
    int NumTracks = FFMS_GetNumTracksI(Indexer);

    for (int i = 0; i < NumTracks; i++) {
        if (FFMS_GetTrackTypeI(Indexer, i) == Type) {
            if (auto CodecName = FFMS_GetCodecNameI(Indexer, i))
                TrackList[i] = CodecName;
        }
    }
    return TrackList;
}

FFmpegSourceProvider::TrackSelection
FFmpegSourceProvider::AskForTrackSelection(const std::map<int, std::string> &TrackList,
                                           FFMS_TrackType Type) {
    std::vector<aegisub_qt::MediaTrackInfo> tracks;
    tracks.reserve(TrackList.size());
    for (auto const &track : TrackList)
        tracks.push_back(aegisub_qt::MediaTrackInfo{track.first, track.second});

    int chosen = -1;
    if (auto selector = aegisub_qt::CopySelector()) {
        const auto kind = Type == FFMS_TYPE_VIDEO ? aegisub_qt::MediaKind::Video
                                                  : aegisub_qt::MediaKind::Audio;
        chosen = selector(kind, tracks);
    }

    // No callback (or an invalid id) means "just take the first track", never a
    // dialog: cancel is handled by the Qt frontend before the provider is even
    // created (two-phase probe -> picker -> open with track id).
    if (chosen < 0 && !tracks.empty())
        chosen = tracks.front().id;

    return static_cast<TrackSelection>(chosen);
}

/// @brief Set ffms2 log level according to setting in config.dat
void FFmpegSourceProvider::SetLogLevel() {
    // Escape hatch for debugging libavformat/libffms2 indexing from the Qt
    // process: AEGISUB_QT_FFMS_DEBUG=1 forces the debug log level.
    if (const char *debug = std::getenv("AEGISUB_QT_FFMS_DEBUG")) {
        if (debug[0] != '\0' && debug[0] != '0') {
            FFMS_SetLogLevel(FFMS_LOG_DEBUG);
            return;
        }
    }

    auto LogLevel = OPT_GET("Provider/FFmpegSource/Log Level")->GetString();
    boost::to_lower(LogLevel);

    if (LogLevel == "panic")
        FFMS_SetLogLevel(FFMS_LOG_PANIC);
    else if (LogLevel == "fatal")
        FFMS_SetLogLevel(FFMS_LOG_FATAL);
    else if (LogLevel == "error")
        FFMS_SetLogLevel(FFMS_LOG_ERROR);
    else if (LogLevel == "warning")
        FFMS_SetLogLevel(FFMS_LOG_WARNING);
    else if (LogLevel == "info")
        FFMS_SetLogLevel(FFMS_LOG_INFO);
    else if (LogLevel == "verbose")
        FFMS_SetLogLevel(FFMS_LOG_VERBOSE);
    else if (LogLevel == "debug")
        FFMS_SetLogLevel(FFMS_LOG_DEBUG);
    else
        FFMS_SetLogLevel(FFMS_LOG_QUIET);
}

FFMS_IndexErrorHandling FFmpegSourceProvider::GetErrorHandlingMode() {
    auto Mode = OPT_GET("Provider/Audio/FFmpegSource/Decode Error Handling")->GetString();
    boost::to_lower(Mode);

    if (Mode == "ignore")
        return FFMS_IEH_IGNORE;
    if (Mode == "clear")
        return FFMS_IEH_CLEAR_TRACK;
    if (Mode == "stop")
        return FFMS_IEH_STOP_TRACK;
    if (Mode == "abort")
        return FFMS_IEH_ABORT;
    return FFMS_IEH_STOP_TRACK; // questionable default?
}

/// @brief  Generates an unique name for the ffms2 index file and prepares the cache folder if it doesn't exist
/// @param filename The name of the source file
/// @return         Returns the generated filename.
agi::fs::path FFmpegSourceProvider::GetCacheFilename(agi::fs::path const& filename) {
    // Get the size of the file to be hashed
    uintmax_t len = agi::fs::Size(filename);

    // Get the hash of the filename
    boost::crc_32_type hash;
    hash.process_bytes(filename.string().c_str(), filename.string().size());

    // Generate the filename
    auto result = config::path->Decode("?local/ffms2cache/" + std::to_string(hash.checksum()) + "_" + std::to_string(len) + "_" + std::to_string(agi::fs::ModifiedTime(filename)) + ".ffindex");

    // Ensure that folder exists
    agi::fs::CreateDirectory(result.parent_path());

    return result;
}

void FFmpegSourceProvider::CleanCache() {
    ::CleanCache(config::path->Decode("?local/ffms2cache/"),
        "*.ffindex",
        OPT_GET("Provider/FFmpegSource/Cache/Size")->GetInt(),
        OPT_GET("Provider/FFmpegSource/Cache/Files")->GetInt());
}

#endif // WITH_FFMS2
