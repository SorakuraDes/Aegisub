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
#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/crc.hpp>
#include <boost/filesystem/path.hpp>

#include <ffms.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace aegisub_qt {
namespace {

std::mutex g_selector_mutex;
TrackSelector g_selector;

TrackSelector CopySelector() {
    std::lock_guard<std::mutex> lock(g_selector_mutex);
    return g_selector;
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

bool ProbeTracks(agi::fs::path const &path, MediaKind kind,
                 std::vector<MediaTrackInfo> *tracks, std::string *error) {
    if (tracks)
        tracks->clear();

    try {
        // ffms2 requires FFMS_Init() to run before any other call (ffms2.h says
        // "Pass 0 to both arguments"). The provider below calls it as well, but
        // FFMS_CreateIndexer must not run first; the wx frontend happened to be
        // fine only because the process had opened a provider earlier.
        FFMS_Init(0, 0);

        char error_buffer[1024] = {};
        FFMS_ErrorInfo error_info;
        error_info.Buffer = error_buffer;
        error_info.BufferSize = sizeof(error_buffer);
        error_info.ErrorType = FFMS_ERROR_SUCCESS;
        error_info.SubType = FFMS_ERROR_SUCCESS;

        // InitializeCore installs the UTF-8 path codecvt on Windows, just as
        // for the shared providers' source and index paths.
        const auto filename_utf8 = path.string();
        FFMS_Indexer *indexer = FFMS_CreateIndexer(filename_utf8.c_str(), &error_info);
        if (!indexer) {
            if (error)
                *error = error_buffer;
            return false;
        }

        // FFMS_CancelIndexing() deletes the indexer, so it must run on every exit
        // path, including the ones that leave through an exception.
        struct IndexerGuard {
            FFMS_Indexer *indexer;
            ~IndexerGuard() {
                if (indexer)
                    FFMS_CancelIndexing(indexer);
            }
        } guard{indexer};

        // GetTracksOfType() only reads the indexer's stream list; the progress
        // runner is not touched, so a provider with no runner is enough here.
        FFmpegSourceProvider probe(nullptr);
        const std::map<int, std::string> list = probe.GetTracksOfType(indexer, ToFFMSType(kind));

        if (tracks) {
            tracks->reserve(list.size());
            for (auto const &track : list)
                tracks->push_back(MediaTrackInfo{track.first, track.second});
        }
        return true;
    } catch (agi::Exception const &ex) {
        if (error)
            *error = ex.GetMessage();
    } catch (std::exception const &ex) {
        if (error)
            *error = ex.what();
    } catch (...) {
        if (error)
            *error = "Failed to probe the media file";
    }
    return false;
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

    // FFMS2 uses the same UTF-8 contract for index paths as for media paths.
    const auto cache_utf8 = CacheName.string();
    LOG_D("ffms/index") << "Writing index: " << cache_utf8;
    if (FFMS_WriteIndex(cache_utf8.c_str(), Index, &ErrInfo))
        LOG_D("ffms/index") << "Could not write index (FFMS error "
                            << ErrInfo.ErrorType << '/' << ErrInfo.SubType << ')';

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

    // Hash the full UTF-8 path; the index basename itself is ASCII.
    const auto filename_utf8 = filename.string();
    boost::crc_32_type hash;
    hash.process_bytes(filename_utf8.data(), filename_utf8.size());

    // Generate the filename
    auto result = config::path->Decode("?local/ffms2cache/" + std::to_string(hash.checksum()) + "_" + std::to_string(len) + "_" + std::to_string(agi::fs::ModifiedTime(filename)) + ".ffindex");

    // Ensure that folder exists. Do NOT use agi::fs::CreateDirectory here: it
    // translates Win32 error codes through POSIX errc constants
    // (libaegisub/common/fs.cpp), so a plain access-denied becomes a
    // FileSystemUnknownError whose localized message then breaks UTF-8-only log
    // converters (docs/qt6-frontend.md pitfalls 56/57). Failing to prepare the
    // cache directory is not fatal: the index is only a cache and the next open
    // simply indexes the file again. Log the numeric code only, never the
    // localized ec.message() text.
    std::error_code ec;
#ifdef _WIN32
    std::filesystem::create_directories(result.parent_path().wstring(), ec);
#else
    std::filesystem::create_directories(result.parent_path().string(), ec);
#endif
    if (ec)
        LOG_D("ffms/index") << "Could not create the ffms2 cache directory (error "
                            << ec.value() << ")";

    return result;
}

void FFmpegSourceProvider::CleanCache() {
    ::CleanCache(config::path->Decode("?local/ffms2cache/"),
        "*.ffindex",
        OPT_GET("Provider/FFmpegSource/Cache/Size")->GetInt(),
        OPT_GET("Provider/FFmpegSource/Cache/Files")->GetInt());
}

#endif // WITH_FFMS2
