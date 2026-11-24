#include "ass_tag_utils.h"

#include "../ass_dialogue.h"
#include "../ass_style.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace aegisub_qt {
namespace {

constexpr std::string_view whitespace = " \t\r\n\f\v";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(whitespace);
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

struct Tag {
    std::string_view name;
    std::string_view args;
    size_t begin, end;
    size_t blockBegin, blockEnd;
    bool parenthesized;
};

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

template<class Visit>
void scanTags(std::string_view text, Visit visit) {
    for (size_t block = 0; (block = text.find('{', block)) != text.npos;) {
        const auto close = text.find('}', block + 1);
        if (close == text.npos) break;
        const auto contents = text.substr(0, close);
        for (size_t begin = block + 1; (begin = contents.find('\\', begin)) != text.npos;) {
            auto name = contents.find_first_not_of(whitespace, begin + 1);
            if (name == text.npos) break;
            auto nameEnd = name;
            while (nameEnd < close && isLetter(text[nameEnd])) ++nameEnd;
            const auto separator = contents.find_first_of("(\\", nameEnd);
            if (separator != text.npos && text[separator] == '(') {
                // Skip the entire argument group, including tags in transforms.
                size_t depth = 1, end = separator + 1;
                for (; end < close && depth; ++end) {
                    if (text[end] == '(') ++depth;
                    else if (text[end] == ')') --depth;
                }
                if (!depth && trim(text.substr(nameEnd, separator - nameEnd)).empty()) {
                    visit(Tag{text.substr(name, nameEnd - name),
                              text.substr(separator + 1, end - separator - 2),
                              begin, end, block, close, true});
                }
                begin = end;
            }
            else {
                const auto end = separator == text.npos ? close : separator;
                visit(Tag{text.substr(name, nameEnd - name), text.substr(nameEnd, end - nameEnd),
                          begin, end, block, close, false});
                begin = end;
            }
        }
        block = close + 1;
    }
}

template<class T>
bool parseNumber(std::string_view arg, T& value, bool consumeAll = true) {
    arg = trim(arg);
    if (!arg.empty() && arg.front() == '+') {
        arg.remove_prefix(1);
        if (!arg.empty() && arg.front() == '-') return false;
    }
    if (arg.empty()) return false;
    const auto end = arg.data() + arg.size();
    const auto result = std::from_chars(arg.data(), end, value);
    if (result.ec != std::errc{} || (consumeAll && result.ptr != end)) return false;
    if constexpr (std::is_floating_point_v<T>) return std::isfinite(value);
    else return true;
}

template<class T, size_t N>
bool parseArgs(const Tag& tag, std::array<T, N>& values) {
    if (!tag.parenthesized) return false;
    auto args = tag.args;
    for (size_t i = 0; i < N; ++i) {
        const auto comma = args.find(',');
        if ((comma == args.npos) != (i == N - 1)) return false;
        if (!parseNumber(args.substr(0, comma), values[i])) return false;
        if (comma != args.npos) args.remove_prefix(comma + 1);
    }
    return true;
}

bool clipTag(const Tag& tag) {
    return tag.parenthesized && (tag.name == "clip" || tag.name == "iclip");
}

bool vectorClip(const Tag& tag) {
    // libass treats one or two arguments as a drawing (optionally scaled).
    const auto comma = tag.args.find(',');
    return comma == tag.args.npos || tag.args.find(',', comma + 1) == tag.args.npos;
}

struct Edit {
    Tag tag;
    std::string replacement;
};

std::string applyEdits(std::string_view text, const std::vector<Edit>& edits) {
    std::string result;
    size_t cursor = 0;
    for (size_t i = 0; i < edits.size();) {
        const auto block = edits[i].tag.blockBegin;
        const auto close = edits[i].tag.blockEnd;
        result += text.substr(cursor, block - cursor);
        auto inside = block + 1;
        const auto before = result.size();
        result += '{';
        do {
            const auto& edit = edits[i++];
            result += text.substr(inside, edit.tag.begin - inside);
            result += edit.replacement;
            inside = edit.tag.end;
        } while (i < edits.size() && edits[i].tag.blockBegin == block);
        result += text.substr(inside, close - inside);
        if (result.size() == before + 1) result.pop_back();
        else result += '}';
        cursor = close + 1;
    }
    result += text.substr(cursor);
    return result;
}

std::string insertLeadingTag(std::string text, const std::string& tag) {
    if (!text.empty() && text.front() == '{') {
        const auto close = text.find('}');
        if (close != std::string::npos) {
            const auto contents = trim(std::string_view(text).substr(1, close - 1));
            if (contents.empty() || contents.front() == '\\') {
                text.insert(1, tag);
                return text;
            }
        }
    }
    return '{' + tag + '}' + text;
}

std::string formatCoord(double value) {
    // Fixed notation keeps the ASS tag locale-independent and free of exponents.
    // This buffer also fits the shortest fixed spelling of any finite double.
    char buffer[384];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 1);
    std::string text(buffer, result.ptr);
    if (text.size() >= 2 && text.compare(text.size() - 2, 2, ".0") == 0)
        text.resize(text.size() - 2);
    return text == "-0" ? "0" : text;
}

std::vector<Edit> rectEdits(std::string_view text, bool* hasVector = nullptr) {
    std::vector<Edit> edits;
    scanTags(text, [&](const Tag& tag) {
        if (!clipTag(tag)) return;
        std::array<int, 4> rect;
        if (tag.name == "clip" && parseArgs(tag, rect)) edits.push_back({tag, {}});
        else if (hasVector && (tag.name == "iclip" || vectorClip(tag))) *hasVector = true;
    });
    return edits;
}

} // namespace

bool parsePos(std::string_view text, double* x, double* y, bool* explicitPos) {
    std::array<double, 2> pos{};
    bool hasPos = false, hasMove = false;
    scanTags(text, [&](const Tag& tag) {
        if (hasPos) return;
        if (tag.name == "pos") {
            std::array<double, 2> candidate;
            if (parseArgs(tag, candidate)) {
                pos = candidate;
                hasPos = true;
            }
        }
        else if (!hasMove && tag.name == "move") {
            std::array<double, 4> move;
            std::array<double, 6> timed;
            if (parseArgs(tag, move)) { pos = {move[0], move[1]}; hasMove = true; }
            else if (parseArgs(tag, timed)) { pos = {timed[0], timed[1]}; hasMove = true; }
        }
    });
    if (explicitPos) *explicitPos = hasPos;
    if (!hasPos && !hasMove) return false;
    if (x) *x = pos[0];
    if (y) *y = pos[1];
    return true;
}

bool parseRectClip(std::string_view text, int* x1, int* y1, int* x2, int* y2) {
    bool found = false;
    scanTags(text, [&](const Tag& tag) {
        std::array<int, 4> rect;
        if (tag.name != "clip" || !parseArgs(tag, rect)) return;
        if (x1) *x1 = rect[0];
        if (y1) *y1 = rect[1];
        if (x2) *x2 = rect[2];
        if (y2) *y2 = rect[3];
        found = true;
    });
    return found;
}

std::string setPosTag(std::string_view text, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::string(text);
    std::vector<Edit> edits;
    scanTags(text, [&](const Tag& tag) {
        if (tag.parenthesized && (tag.name == "pos" || tag.name == "move"))
            edits.push_back({tag, {}});
    });
    const auto position = "\\pos(" + formatCoord(x) + ',' + formatCoord(y) + ')';
    for (auto& edit : edits) {
        if (edit.tag.blockBegin == 0 && edit.tag.name == "pos") {
            edit.replacement = position;
            return applyEdits(text, edits);
        }
    }
    return insertLeadingTag(applyEdits(text, edits), position);
}

std::string setRectClipTag(std::string_view text, int x1, int y1, int x2, int y2) {
    bool hasVector = false;
    auto edits = rectEdits(text, &hasVector);
    if (edits.empty() && hasVector) return std::string(text);
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    const auto tag = "\\clip(" + std::to_string(x1) + ',' + std::to_string(y1) + ',' +
                     std::to_string(x2) + ',' + std::to_string(y2) + ')';
    if (edits.empty()) return insertLeadingTag(std::string(text), tag);
    edits.back().replacement = tag;
    return applyEdits(text, edits);
}

std::string removeRectClipTag(std::string_view text) {
    return applyEdits(text, rectEdits(text));
}

std::pair<double, double> defaultAnchorPos(const AssDialogue& line, const AssStyle* style,
                                           int scriptWidth, int scriptHeight) {
    const std::string_view text = line.Text.get();
    double x, y;
    if (parsePos(text, &x, &y)) return {x, y};

    bool hasAn = false, hasA = false;
    int an = 0, a = 2;
    scanTags(text, [&](const Tag& tag) {
        if (!hasAn && tag.name == "an") {
            parseNumber(tag.args, an, false);
            hasAn = true;
        }
        else if (!hasA && tag.name == "a") {
            parseNumber(tag.args, a, false);
            hasA = true;
        }
    });
    auto margin = line.Margin;
    int align = 2;
    if (style) {
        align = style->alignment;
        for (size_t i = 0; i < margin.size(); ++i)
            if (margin[i] == 0) margin[i] = style->Margin[i];
    }
    const int overrideAlign = hasAn ? an : hasA ? AssStyle::SsaToAss(a) : 0;
    if (overrideAlign >= 1 && overrideAlign <= 9) align = overrideAlign;
    if (align < 1 || align > 9) align = 2;

    const int hor = (align - 1) % 3;
    const int vert = (align - 1) / 3;
    const auto width = static_cast<int64_t>(scriptWidth);
    const auto height = static_cast<int64_t>(scriptHeight);
    // wx uses integer division for both centered coordinates.
    x = static_cast<double>(hor == 0 ? margin[0] : hor == 1 ? (width + margin[0] - margin[1]) / 2 : width - margin[1]);
    y = static_cast<double>(vert == 0 ? height - margin[2] : vert == 1 ? height / 2 : margin[2]);
    return {x, y};
}

} // namespace aegisub_qt
