// Standalone test executable: compile with ass_tag_utils.cpp and link the ASS core.
#include "ass_tag_utils.h"

#include "../ass_dialogue.h"
#include "../ass_style.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void check(bool condition, const char* test) {
    if (condition) return;
    std::cerr << test << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using namespace aegisub_qt;

    double x = 1, y = 2;
    check(!parsePos(R"(plain \pos(8,9){\t(0,20,\pos(12,15))}{\position(3,4)}{\pos(1x,2)}{\pos(4,7)", &x, &y)
          && x == 1 && y == 2, "reject text, nested, near-name and malformed positions");
    check(parsePos(R"({\pos(nan,1)}{\pos( +12.5 , -.25 )\pos(6,7)})", &x, &y)
          && x == 12.5 && y == -0.25, "first valid position and signed decimal numbers");
    check(parsePos(R"({\pos(1,2)})", nullptr, nullptr), "null position outputs");
    const auto positioned = setPosTag(R"({\t(0,20,\pos(5,6)\move(1,2,3,4))\move(2,3,4,5)}text{\pos(1,2)\b1})", 9.5, -0.25);
    check(positioned == R"({\pos(9.5,-0.2)\t(0,20,\pos(5,6)\move(1,2,3,4))}text{\b1})",
          "place static position first, strip only top-level positions and moves");
    check(setPosTag(positioned, 9.5, -0.25) == positioned, "stable position rewrite");
    check(setPosTag(R"({comment}text{\move(2,3,4,5,10,20)})", 1, 2) == R"({\pos(1,2)}{comment}text)",
          "timed move conversion retains original comment");
    check(setPosTag("text", std::numeric_limits<double>::infinity(), 0) == "text", "reject nonfinite coordinates");

    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    const std::string vector = R"({\iclip(2,m 0 0 l 1 0 1 1 0 1)}shape)", mixed =
        R"({\clip(1,2,3,4)}{\clip(m 0 0 l 1 1)\t(\clip(2,3,4,5))\iclip( +6,-7,8,9)}text)";
    check(!parseRectClip(vector, &x1, &y1, &x2, &y2) && x1 == 0 && y2 == 0,
          "vector clip is not a rectangle and does not modify outputs");
    check(setRectClipTag(vector, 1, 2, 3, 4) == vector && removeRectClipTag(vector) == vector,
          "vector-only conflict leaves the line unchanged");
    check(parseRectClip(mixed, &x1, &y1, &x2, &y2) && x1 == 1 && y1 == 2 && x2 == 3 && y2 == 4,
          "ignore inverse and nested rectangles");
    check(!parseRectClip(R"({\clip(1,2,3,2147483648)\clip(1,2,,4)\t(\clip(1,2,3,4))})", nullptr, nullptr, nullptr, nullptr),
          "reject overflow, missing arguments and nested rectangles");
    check(setRectClipTag(mixed, 30, 40, 10, 20) == R"({\clip(10,20,30,40)}{\clip(m 0 0 l 1 1)\t(\clip(2,3,4,5))\iclip( +6,-7,8,9)}text)",
          "normalize coordinates and replace rectangles in place");
    check(removeRectClipTag(mixed) == R"({\clip(m 0 0 l 1 1)\t(\clip(2,3,4,5))\iclip( +6,-7,8,9)}text)",
          "remove ordinary rectangles without changing other clip types");
    check(setPosTag(R"({\i1\pos(24,8)\fad(10,20)}text)", 2, 3) == R"({\i1\pos(2,3)\fad(10,20)}text)",
          "leading position is replaced in place");
    bool explicitPos = true;
    check(parsePos(R"({\move(-1,2.5,3,4)})", &x, &y, &explicitPos) && x == -1 && y == 2.5 && !explicitPos,
          "fall back to the first point of a move");
    check(setRectClipTag("text", 1, 2, 3, 4) == R"({\clip(1,2,3,4)}text)", "add rectangle to an unclipped line");

    AssDialogue line;
    AssStyle style;
    line.Margin = {0, 5, 0};
    style.Margin = {11, 22, 33};
    style.alignment = 9;
    check(defaultAnchorPos(line, &style, 641, 481) == std::pair<double,double>{636, 33}, "style margins and alignment");
    check(defaultAnchorPos(line, nullptr, 642, 481) == std::pair<double,double>{318, 481}, "null style and integer center");
    line.Text = R"({\a5\t(\an9)}text{\an5})";
    check(defaultAnchorPos(line, &style, 641, 481) == std::pair<double,double>{323, 240}, "modern alignment and center");
    line.Text = R"({\a11}text)";
    check(defaultAnchorPos(line, &style, 641, 481) == std::pair<double,double>{636, 240}, "legacy SSA alignment mapping");
    line.Text = R"({\move(4.5,6,7,8,10,20)\an9}text)";
    check(defaultAnchorPos(line, &style, 641, 481) == std::pair<double,double>{4.5, 6}, "moving line uses its start anchor");
    line.Text = R"({\move(4.5,6,7,8)\pos(10,20)}text)";
    check(defaultAnchorPos(line, &style, 641, 481) == std::pair<double,double>{10, 20}, "explicit pos has wx anchor precedence");
}
