#include "fonts_collector_bridge.h"

#ifdef _WIN32
#include <windows.h>

#include "compat.h"
#include "font_file_lister.h"
#include "format.h"

#include <libaegisub/exception.h>
#include <wx/string.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#endif

namespace aegisub_qt {
#ifdef _WIN32
namespace {
namespace fs = std::filesystem;
using Log = std::function<void(std::string)>;

void Emit(Log const& log, std::string message) {
    if (log) log(std::move(message));
}

// Use the original formatter and message IDs, including the newline. Without
// a wx translation catalog the formatter uses the original English strings.
struct DiagnosticPattern {
    std::string prefix;
    std::string suffix;

    explicit DiagnosticPattern(wxString const& formatted) {
        auto text = from_wx(formatted);
        auto slot = text.find("\x1f");
        if (slot == std::string::npos) prefix = std::move(text);
        else {
            prefix = text.substr(0, slot);
            suffix = text.substr(slot + 1);
        }
    }

    bool Matches(std::string const& text) const {
        return text.size() >= prefix.size() + suffix.size()
            && text.compare(0, prefix.size(), prefix) == 0
            && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
};

void WarnAboutAttachmentSize(Log const& log, std::uintmax_t totalSize) {
    if (totalSize > 32 * 1024 * 1024)
        Emit(log, "Over 32 MiB of fonts were copied. Some may not be loaded by the player if all are attached to a Matroska file.\n");
}

void CreateDirectories(fs::path const& folder) {
    if (folder.empty()) return;
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) throw std::runtime_error("Cannot create directory " + folder.u8string()
        + " (error " + std::to_string(ec.value()) + ")");
}

void OutputToFolder(std::vector<fs::path> const& paths, fs::path const& directory,
    Log const& log, bool& failed) {
    CreateDirectories(directory);
    Emit(log, "Copying fonts to folder...\n");
    std::uintmax_t totalSize = 0;
    for (auto const& path : paths) {
        auto target = directory / path.filename();
        std::error_code ec;
        if (fs::is_regular_file(target, ec))
            Emit(log, "* " + target.filename().u8string() + " already exists on destination.\n");
        else if (fs::copy_file(path, target, fs::copy_options::skip_existing, ec)) {
            auto size = fs::file_size(target, ec);
            if (!ec) totalSize += size;
            Emit(log, "* Copied " + path.u8string() + ".\n");
        }
        else if (!ec && fs::is_regular_file(target, ec))
            Emit(log, "* " + target.filename().u8string() + " already exists on destination.\n");
        else {
            failed = true;
            Emit(log, "* Failed to copy " + path.u8string() + ".\n");
        }
    }
    Emit(log, failed ? "Done. Some fonts could not be copied.\n" : "Done. All fonts copied.\n");
    WarnAboutAttachmentSize(log, totalSize);
}

[[noreturn]] void WindowsError(char const* action, DWORD code = GetLastError()) {
    throw std::runtime_error(std::string(action) + " (Win32 error " + std::to_string(code) + ")");
}

// Keep the staging file next to the destination; never truncate the existing
// archive, and never fall back to a cross-volume copy during publication.
class StagedArchive {
    fs::path path;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::uint64_t offset = 0;

public:
    explicit StagedArchive(fs::path const& destination) {
        auto folder = destination.parent_path();
        static std::atomic<unsigned> sequence{0};
        for (int i = 0; i < 128; ++i) {
            path = folder / (L".aegisub-fonts-" + std::to_wstring(GetCurrentProcessId())
                + L"-" + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp");
            if (CompareStringOrdinal(path.c_str(), -1, destination.c_str(), -1, TRUE) == CSTR_EQUAL)
                continue;
            file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) return;
            auto code = GetLastError();
            if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
                WindowsError("Cannot create temporary ZIP", code);
        }
        throw std::runtime_error("Cannot reserve a temporary ZIP filename");
    }

    StagedArchive(StagedArchive const&) = delete;
    StagedArchive& operator=(StagedArchive const&) = delete;

    ~StagedArchive() {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (!path.empty()) DeleteFileW(path.c_str());
    }

    std::uint32_t Offset() const { return static_cast<std::uint32_t>(offset); }

    void Write(void const* data, std::size_t size) {
        if (offset + size >= UINT32_MAX)
            throw std::runtime_error("ZIP exceeds the classic ZIP size limit");
        if (!size) return;
        DWORD written = 0;
        if (!WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr))
            WindowsError("Cannot write temporary ZIP");
        if (written != size)
            throw std::runtime_error("Incomplete write to temporary ZIP");
        offset += size;
    }

    void U16(std::uint16_t value) {
        unsigned char bytes[]{static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8)};
        Write(bytes, sizeof bytes);
    }

    void U32(std::uint32_t value) {
        unsigned char bytes[]{static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
            static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
        Write(bytes, sizeof bytes);
    }

    void Commit(fs::path const& destination) {
        if (!FlushFileBuffers(file)) WindowsError("Cannot flush temporary ZIP");
        if (!CloseHandle(file)) WindowsError("Cannot close temporary ZIP");
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            WindowsError("Cannot replace destination ZIP");
        path.clear();
    }
};

struct ZipEntry {
    enum class Method : std::uint16_t { Store = 0, Deflate = 8 };

    std::string name;
    std::uint32_t headerOffset;
    Method method = Method::Deflate;
    std::uint32_t crc = 0;
    std::uint32_t packedSize = 0;
    std::uint32_t unpackedSize = 0;

    void WriteCommonFields(StagedArchive& archive, bool central) const {
        bool hasDescriptor = method == Method::Deflate;
        bool hasSizes = central || !hasDescriptor;
        archive.U16(hasDescriptor ? 20 : 10); // Version needed to extract
        archive.U16(hasDescriptor ? 0x0808 : 0x0800); // UTF-8, optional data descriptor
        archive.U16(static_cast<std::uint16_t>(method));
        archive.U16(0);      // DOS time 00:00:00
        archive.U16(0x0021); // DOS date 1980-01-01
        archive.U32(hasSizes ? crc : 0);
        archive.U32(hasSizes ? packedSize : 0);
        archive.U32(hasSizes ? unpackedSize : 0);
        archive.U16(static_cast<std::uint16_t>(name.size()));
        archive.U16(0);      // Extra field length
    }

    void WriteLocal(StagedArchive& archive) const {
        archive.U32(0x04034b50);
        WriteCommonFields(archive, false);
        archive.Write(name.data(), name.size());
    }

    void WriteCentral(StagedArchive& archive) const {
        archive.U32(0x02014b50);
        archive.U16(20);     // MS-DOS/Windows archive, version 2.0
        WriteCommonFields(archive, true);
        archive.U16(0);      // Comment length
        archive.U16(0);      // Start disk
        archive.U16(0);      // Internal file attributes
        archive.U32(0);      // External file attributes
        archive.U32(headerOffset);
        archive.Write(name.data(), name.size());
    }
};

struct Deflater {
    z_stream stream{};

    Deflater() {
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
            Z_DEFAULT_STRATEGY) != Z_OK)
            throw std::runtime_error("Cannot initialize ZIP compression");
    }

    Deflater(Deflater const&) = delete;
    Deflater& operator=(Deflater const&) = delete;
    ~Deflater() { deflateEnd(&stream); }
};

ZipEntry WriteZipEntry(StagedArchive& archive, fs::path const& path) {
    auto name = path.filename().u8string();
    if (name.empty() || name.size() > UINT16_MAX || name == "." || name == ".."
        || name.find_first_of("/\\:") != std::string::npos)
        throw std::runtime_error("Invalid ZIP entry filename");

    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read font file");
    ZipEntry entry{std::move(name), archive.Offset()};
    std::array<unsigned char, 64 * 1024> in, out;
    auto crc = crc32(0, Z_NULL, 0);

    // Probe at most 4 KiB plus one byte, then rewind only for streaming DEFLATE.
    // STORE can write final CRC and sizes directly into the local header.
    input.read(reinterpret_cast<char*>(in.data()), 4 * 1024 + 1);
    if (input.bad() || (input.fail() && !input.eof()))
        throw std::runtime_error("Error reading font file");
    if (input.eof()) {
        auto read = static_cast<uInt>(input.gcount());
        entry.method = ZipEntry::Method::Store;
        entry.packedSize = entry.unpackedSize = read;
        entry.crc = static_cast<std::uint32_t>(crc32(crc, in.data(), read));
        entry.WriteLocal(archive);
        archive.Write(in.data(), read);
        return entry;
    }
    if (!input.seekg(0)) throw std::runtime_error("Cannot rewind font file");

    entry.WriteLocal(archive);
    auto dataStart = archive.Offset();
    Deflater deflater;
    auto& stream = deflater.stream;
    int status;
    do {
        input.read(reinterpret_cast<char*>(in.data()), in.size());
        if (input.bad() || (input.fail() && !input.eof()))
            throw std::runtime_error("Error reading font file");
        auto read = static_cast<uInt>(input.gcount());
        if (read >= UINT32_MAX - entry.unpackedSize)
            throw std::runtime_error("Font exceeds the classic ZIP size limit");
        entry.unpackedSize += read;
        crc = crc32(crc, in.data(), read);
        stream.next_in = in.data();
        stream.avail_in = read;
        do {
            stream.next_out = out.data();
            stream.avail_out = static_cast<uInt>(out.size());
            status = deflate(&stream, input.eof() ? Z_FINISH : Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END)
                throw std::runtime_error("Error compressing font file");
            archive.Write(out.data(), out.size() - stream.avail_out);
        } while (stream.avail_in || (input.eof() && status != Z_STREAM_END));
    } while (status != Z_STREAM_END);

    entry.crc = static_cast<std::uint32_t>(crc);
    entry.packedSize = archive.Offset() - dataStart;
    archive.U32(0x08074b50);
    archive.U32(entry.crc);
    archive.U32(entry.packedSize);
    archive.U32(entry.unpackedSize);
    return entry;
}

void OutputToZip(std::vector<fs::path> const& paths, fs::path const& destination, Log const& log) {
    if (paths.size() >= UINT16_MAX)
        throw std::runtime_error("Too many fonts for a classic ZIP archive");
    std::error_code ec;
    auto target = fs::absolute(destination, ec);
    if (ec) throw std::runtime_error("Cannot resolve ZIP destination (error " + std::to_string(ec.value()) + ")");
    target = target.lexically_normal().make_preferred();
    if (!target.has_filename() || target.filename() == L".")
        throw std::runtime_error("No ZIP filename specified");
    CreateDirectories(target.parent_path());
    Emit(log, "Copying fonts to archive...\n");

    StagedArchive archive(target);
    std::vector<ZipEntry> entries;
    std::vector<std::wstring> names;
    for (auto const& path : paths) {
        auto name = path.filename().native();
        // A case-insensitive collision cannot be extracted into one Windows folder.
        if (std::any_of(names.begin(), names.end(), [&](std::wstring const& existing) {
            return CompareStringOrdinal(existing.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL;
        }))
            throw std::runtime_error("Fonts share a ZIP entry filename: " + path.filename().u8string());
        names.push_back(std::move(name));
        try {
            entries.push_back(WriteZipEntry(archive, path));
        }
        catch (std::runtime_error const& err) {
            throw std::runtime_error(path.u8string() + ": " + err.what());
        }
    }

    auto centralStart = archive.Offset();
    std::uintmax_t totalSize = 0;
    for (auto const& entry : entries) {
        entry.WriteCentral(archive);
        totalSize += entry.unpackedSize;
    }
    auto centralSize = archive.Offset() - centralStart;
    auto count = static_cast<std::uint16_t>(entries.size());
    archive.U32(0x06054b50);
    archive.U16(0); // Disk number
    archive.U16(0); // Disk holding the central directory
    archive.U16(count);
    archive.U16(count);
    archive.U32(centralSize);
    archive.U32(centralStart);
    archive.U16(0); // Archive comment length
    archive.Commit(target);

    for (auto const& path : paths) Emit(log, "* Copied " + path.u8string() + ".\n");
    Emit(log, "Done. All fonts copied.\n");
    WarnAboutAttachmentSize(log, totalSize);
}
} // namespace
#endif

FontsCollectorResult CollectFonts(AssFile const& snapshot, int action,
    std::filesystem::path const& destination, std::function<void(std::string)> const& log) {
    FontsCollectorResult result;
#ifdef _WIN32
    try {
        DiagnosticPattern missingFont(fmt_tl("Could not find font '%s'\n", "\x1f"));
        DiagnosticPattern missingStyle(fmt_tl("Style '%s' does not exist\n", "\x1f"));
        auto scanned = FontCollector([&](wxString message, int colour) {
            auto text = from_wx(message);
            if (colour == 2 && (missingFont.Matches(text) || missingStyle.Matches(text)))
                ++result.missingCount;
            Emit(log, std::move(text));
        }).GetFontPaths(&snapshot);
        result.paths.reserve(scanned.size());
        for (auto const& path : scanned) result.paths.emplace_back(path.native());
    }
    catch (agi::Exception const& err) {
        result.outputFailed = true;
        Emit(log, "* An error occurred when enumerating the used fonts: " + err.GetMessage() + "\n");
        return result;
    }

    if (action < 0 || action > 3) {
        result.outputFailed = true;
        Emit(log, "* Invalid Fonts Collector action.\n");
    }
    else if (action != 0 && !result.paths.empty()) {
        try {
            if (destination.empty()) throw std::runtime_error("No destination specified");
            if (action == 3) OutputToZip(result.paths, destination, log);
            else OutputToFolder(result.paths, destination, log, result.outputFailed);
        }
        catch (std::runtime_error const& err) {
            result.outputFailed = true;
            Emit(log, "* Failed to export fonts: " + std::string(err.what()) + "\n");
        }
    }
#else
    (void)snapshot;
    (void)action;
    (void)destination;
    result.outputFailed = true;
    if (log) log("* Fonts Collector is not supported on this platform.\n");
#endif
    return result;
}

} // namespace aegisub_qt
