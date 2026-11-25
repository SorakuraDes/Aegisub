#include "ass_tag_utils.h"

#include "../ass_dialogue.h"
#include "../ass_style.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
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
void scanTagRange(std::string_view text, size_t begin, size_t limit, size_t block, size_t close,
                  unsigned transformDepth, Visit& visit) {
    const auto contents = text.substr(0, limit);
    while ((begin = contents.find('\\', begin)) != text.npos) {
        const auto name = contents.find_first_not_of(whitespace, begin + 1);
        if (name == text.npos) break;
        auto nameEnd = name;
        while (nameEnd < limit && isLetter(text[nameEnd])) ++nameEnd;
        const auto tagName = text.substr(name, nameEnd - name);
        const auto separator = contents.find_first_of("(\\", nameEnd);
        if (separator != text.npos && text[separator] == '(') {
            size_t depth = 1, end = separator + 1;
            for (; end < limit && depth; ++end) {
                if (text[end] == '(') ++depth;
                else if (text[end] == ')') --depth;
            }
            if (!depth && trim(text.substr(nameEnd, separator - nameEnd)).empty()) {
                visit(Tag{tagName, text.substr(separator + 1, end - separator - 2),
                          begin, end, block, close, true});
                if (tagName == "t" && transformDepth)
                    scanTagRange(text, separator + 1, end - 1, block, close, transformDepth - 1, visit);
            }
            begin = end;
        }
        else {
            const auto end = separator == text.npos ? limit : separator;
            visit(Tag{tagName, text.substr(nameEnd, end - nameEnd), begin, end, block, close, false});
            begin = end;
        }
    }
}

template<class Visit>
void scanTags(std::string_view text, Visit visit, unsigned transformDepth = 0) {
    for (size_t block = 0; (block = text.find('{', block)) != text.npos;) {
        const auto close = text.find('}', block + 1);
        if (close == text.npos) break;
        scanTagRange(text, block + 1, close, block, close, transformDepth, visit);
        block = close + 1;
    }
}

template<class T>
bool parseNumber(std::string_view arg, T& value, bool consumeAll = true, size_t* consumed = nullptr) {
    const auto original = arg;
    arg = trim(arg);
    if (!arg.empty() && arg.front() == '+') {
        arg.remove_prefix(1);
        if (!arg.empty() && arg.front() == '-') return false;
    }
    if (arg.empty()) return false;
    const auto end = arg.data() + arg.size();
    const auto result = std::from_chars(arg.data(), end, value);
    if (result.ec != std::errc{} || (consumeAll && result.ptr != end)) return false;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) return false;
    }
    if (consumed) *consumed = result.ptr - original.data();
    return true;
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

struct MoveArgs {
    std::array<double, 4> points;
    int t1 = 0, t2 = 0;
    std::string_view timing;
};

bool readMoveArgs(const Tag& tag, MoveArgs& move) {
    if (tag.name != "move" || !tag.parenthesized) return false;
    MoveArgs candidate;
    auto args = tag.args;
    for (size_t i = 0; i < candidate.points.size(); ++i) {
        const auto comma = args.find(',');
        if (!parseNumber(args.substr(0, comma), candidate.points[i])) return false;
        if (comma == args.npos) {
            if (i + 1 != candidate.points.size()) return false;
            move = candidate;
            return true;
        }
        if (i + 1 == candidate.points.size()) candidate.timing = args.substr(comma);
        args.remove_prefix(comma + 1);
    }
    const auto comma = args.find(',');
    if (comma == args.npos || !parseNumber(args.substr(0, comma), candidate.t1) ||
        !parseNumber(args.substr(comma + 1), candidate.t2)) return false;
    move = candidate;
    return true;
}

bool rotationZTag(const Tag& tag) {
    return tag.name == "frz" || tag.name == "fr";
}

bool readScalar(const Tag& tag, double& value, size_t* consumed = nullptr) {
    double candidate;
    if (tag.parenthesized || !parseNumber(tag.args, candidate, false, consumed)) return false;
    value = candidate;
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

Tag scalarEditRange(Tag tag) {
    if (tag.parenthesized) return tag;
    double value;
    size_t consumed = 0;
    readScalar(tag, value, &consumed);
    // Bare scalar tags may be followed by override-block comments or spaces.
    tag.end = tag.end - tag.args.size() + consumed;
    return tag;
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
        else if (!hasMove) {
            MoveArgs move;
            if (readMoveArgs(tag, move)) {
                pos = {move.points[0], move.points[1]};
                hasMove = true;
            }
        }
    });
    if (explicitPos) *explicitPos = hasPos;
    if (!hasPos && !hasMove) return false;
    if (x) *x = pos[0];
    if (y) *y = pos[1];
    return true;
}

bool parseMove(std::string_view text, double* x1, double* y1, double* x2, double* y2,
               int* t1, int* t2) {
    MoveArgs move;
    bool found = false;
    scanTags(text, [&](const Tag& tag) {
        if (!found) found = readMoveArgs(tag, move);
    });
    if (!found) return false;
    if (x1) *x1 = move.points[0];
    if (y1) *y1 = move.points[1];
    if (x2) *x2 = move.points[2];
    if (y2) *y2 = move.points[3];
    if (t1) *t1 = move.t1;
    if (t2) *t2 = move.t2;
    return true;
}

bool parseOrg(std::string_view text, double* x, double* y) {
    bool found = false;
    scanTags(text, [&](const Tag& tag) {
        std::array<double, 2> org;
        if (found || tag.name != "org" || !parseArgs(tag, org)) return;
        if (x) *x = org[0];
        if (y) *y = org[1];
        found = true;
    });
    return found;
}

bool parseRotationZ(std::string_view text, const AssStyle* style, double* angleDeg) {
    double angle = style ? style->angle : 0.0;
    bool hasFrz = false, hasFr = false;
    scanTags(text, [&](const Tag& tag) {
        if (hasFrz) return;
        if (tag.name == "frz") hasFrz = readScalar(tag, angle);
        else if (tag.name == "fr" && !hasFr) hasFr = readScalar(tag, angle);
    });
    if (angleDeg) *angleDeg = angle;
    return hasFrz || hasFr || style;
}

bool parseScale(std::string_view text, const AssStyle* style, double* x, double* y) {
    double scaleX = style ? style->scalex : 100.0;
    double scaleY = style ? style->scaley : 100.0;
    bool hasX = false, hasY = false;
    scanTags(text, [&](const Tag& tag) {
        if (tag.name == "fscx" && !hasX) hasX = readScalar(tag, scaleX);
        else if (tag.name == "fscy" && !hasY) hasY = readScalar(tag, scaleY);
    });
    if (x) *x = scaleX;
    if (y) *y = scaleY;
    return hasX || hasY || style;
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

std::string setMoveTag(std::string_view text, double x1, double y1, double x2, double y2) {
    if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2))
        return std::string(text);
    std::vector<Edit> edits;
    auto firstPos = text.npos, firstMove = text.npos;
    MoveArgs move;
    scanTags(text, [&](const Tag& tag) {
        if (!tag.parenthesized || (tag.name != "pos" && tag.name != "move")) return;
        if (tag.name == "pos" && firstPos == text.npos) firstPos = edits.size();
        if (firstMove == text.npos && readMoveArgs(tag, move)) firstMove = edits.size();
        edits.push_back({tag, {}});
    });
    auto replacement = "\\move(" + formatCoord(x1) + ',' + formatCoord(y1) + ',' +
                       formatCoord(x2) + ',' + formatCoord(y2);
    replacement += move.timing;
    replacement += ')';
    const auto index = firstMove != text.npos ? firstMove : firstPos;
    if (index == text.npos) {
        if (edits.empty()) return insertLeadingTag(std::string(text), replacement);
        edits.front().replacement = std::move(replacement);
    }
    else edits[index].replacement = std::move(replacement);
    return applyEdits(text, edits);
}

std::string setRotationZTag(std::string_view text, double angleDeg) {
    if (!std::isfinite(angleDeg)) return std::string(text);
    angleDeg = std::fmod(angleDeg, 360.0);
    if (angleDeg > 180.0) angleDeg -= 360.0;
    else if (angleDeg < -180.0) angleDeg += 360.0;
    const auto replacement = "\\frz" + formatCoord(angleDeg);
    std::vector<Edit> edits;
    scanTags(text, [&](const Tag& tag) {
        if (rotationZTag(tag)) edits.push_back({scalarEditRange(tag), edits.empty() ? replacement : std::string{}});
    });
    if (edits.empty()) return insertLeadingTag(std::string(text), replacement);
    return applyEdits(text, edits);
}

std::string setScaleTags(std::string_view text, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::string(text);
    const auto xTag = "\\fscx" + formatCoord(x < 1.0 ? 1.0 : x);
    const auto yTag = "\\fscy" + formatCoord(y < 1.0 ? 1.0 : y);
    std::vector<Edit> edits;
    bool hasX = false, hasY = false;
    scanTags(text, [&](const Tag& tag) {
        if (tag.name == "fscx") {
            edits.push_back({scalarEditRange(tag), hasX ? std::string{} : xTag});
            hasX = true;
        }
        else if (tag.name == "fscy") {
            edits.push_back({scalarEditRange(tag), hasY ? std::string{} : yTag});
            hasY = true;
        }
    });
    auto result = applyEdits(text, edits);
    if (hasX && hasY) return result;
    return insertLeadingTag(std::move(result), (hasX ? std::string{} : xTag) + (hasY ? std::string{} : yTag));
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

std::string resampleLineTags(std::string_view text, double scaleX, double scaleY) {
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 ||
        (scaleX == 1 && scaleY == 1)) return std::string(text);

    // Fixed notation is understood by libass and avoids binary floating-point noise.
    const auto format = [](double value) {
        char buffer[384];
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 6);
        auto end = result.ptr;
        while (end[-1] == '0') --end;
        if (end[-1] == '.') --end;
        std::string number(buffer, end);
        return number == "-0" ? std::string("0") : number;
    };

    std::vector<Edit> edits;
    scanTags(text, [&](Tag tag) {
        if (tag.name == "fs" && scaleY != 1 && !tag.parenthesized) {
            const auto numberStart = tag.args.find_first_not_of(whitespace);
            if (numberStart == tag.args.npos || tag.args[numberStart] == '+' || tag.args[numberStart] == '-') return;
            double size;
            size_t consumed;
            if (!readScalar(tag, size, &consumed) || !std::isfinite(size * scaleY)) return;
            if (consumed < tag.args.size() && whitespace.find(tag.args[consumed]) == whitespace.npos) return;
            const auto argStart = tag.end - tag.args.size();
            tag.begin = argStart + numberStart;
            tag.end = argStart + consumed;
            edits.push_back({tag, format(size * scaleY)});
            return;
        }

        std::array<double, 4> coords;
        size_t count;
        const bool rect = clipTag(tag);
        std::string_view timing;
        if (rect) {
            std::array<int, 4> clip;
            if (!parseArgs(tag, clip)) return;
            std::copy(clip.begin(), clip.end(), coords.begin());
            count = clip.size();
        }
        else if (tag.name == "pos" || tag.name == "org") {
            std::array<double, 2> point;
            if (!parseArgs(tag, point)) return;
            std::copy(point.begin(), point.end(), coords.begin());
            count = point.size();
        }
        else if (tag.name == "move") {
            MoveArgs move;
            if (!readMoveArgs(tag, move)) return;
            coords = move.points;
            count = coords.size();
            timing = move.timing;
        }
        else return;

        std::string replacement = "\\";
        replacement += tag.name;
        replacement += '(';
        for (size_t i = 0; i < count; ++i) {
            const double value = coords[i] * (i % 2 == 0 ? scaleX : scaleY);
            if (!std::isfinite(value)) return;
            if (i) replacement += ',';
            if (rect) {
                const auto rounded = std::clamp(std::round(value), double(std::numeric_limits<int>::min()),
                                                double(std::numeric_limits<int>::max()));
                replacement += std::to_string(static_cast<int>(rounded));
            }
            else replacement += format(value);
        }
        replacement += timing;
        replacement += ')';
        edits.push_back({tag, std::move(replacement)});
    }, 16); // bound recursion for malformed or excessively nested transforms
    return applyEdits(text, edits);
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
