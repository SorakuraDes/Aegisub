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

    double mx1 = 99, my1 = 99, mx2 = 99, my2 = 99;
    int t1 = 99, t2 = 99;
    check(!parseMove(R"({\t(\move(1,2,3,4))\move(1,2,3,4,5)\move(1,2,3,4,0,2147483648)})",
                     &mx1, &my1, &mx2, &my2, &t1, &t2) && mx1 == 99 && my2 == 99 && t1 == 99 && t2 == 99,
          "ignore nested and malformed moves without changing outputs");
    check(parseMove(R"({\move(-1,2.5,+3,4)})", &mx1, &my1, &mx2, &my2, &t1, &t2)
          && mx1 == -1 && my1 == 2.5 && mx2 == 3 && my2 == 4 && t1 == 0 && t2 == 0,
          "four-argument move has no explicit timing");
    check(parseMove(R"({\move(1,2,3,4, +010 , 800)\move(9,8,7,6)})", &mx1, nullptr, nullptr, nullptr, &t1, &t2)
          && mx1 == 1 && t1 == 10 && t2 == 800, "parse the first timed move with optional outputs");
    // Universal escapes keep the fixture independent of the source code page.
    const std::string chinese = "\u4e2d\u6587";
    check(setMoveTag(chinese + R"({\i1\pos(6,7)\t(\pos(2,3)\move(4,5,6,7))}x{\pos(8,9)\b1})",
                     1, -2.26, 3.44, 4) == chinese + R"({\i1\move(1,-2.3,3.4,4)\t(\pos(2,3)\move(4,5,6,7))}x{\b1})",
          "convert a mid-line position in place and remove only top-level competitors");
    check(setMoveTag(R"({\pos(1,2)\b1\move(3,4,5,6, +000 , 0 )}{\move(7,8,9,10)})" + chinese,
                     12, 13, 14, 15) == R"({\b1\move(12,13,14,15, +000 , 0 )})" + chinese,
          "replace an existing move in place and retain its timing spelling, including zero times");
    check(setMoveTag(chinese, 1, 2, 3, 4) == R"({\move(1,2,3,4)})" + chinese, "insert an untimed move");

    AssDialogue line;
    AssStyle style;
    style.angle = 29.5;
    style.scalex = 106;
    style.scaley = 94.5;
    double angle = 0;
    check(parseRotationZ(R"({\frx40\fry20\t(\frz10)\frz nan})", &style, &angle) && angle == 29.5,
          "rotation falls back to style, ignoring invalid and non-Z rotations");
    check(parseRotationZ(R"({\fr-40.25\fry10})", &style, &angle) && angle == -40.25,
          "parse legacy fr rotation");
    check(parseRotationZ(R"({\fr-40.25\frz +90\frz100})", nullptr, &angle) && angle == 90,
          "first valid frz takes precedence over fr");
    check(!parseRotationZ(chinese, nullptr, &angle) && angle == 0, "default rotation is zero without a style");
    check(parseOrg(R"({\org(4)\t(\org(1,2))\org(+2.25,-5)\org(8,9)})", &x, &y)
          && x == 2.25 && y == -5, "parse the first valid top-level origin");
    check(!parseOrg(R"({\t(\org(1,2))})", &x, &y) && x == 2.25 && y == -5, "no default origin");
    const auto rotated = setRotationZTag(R"({\frx10\fr20 )" + chinese + R"(\fry30\t(\fr12\frz9)}{\frz0}text)", 540.25);
    check(rotated == R"({\frx10\frz-179.8 )" + chinese + R"(\fry30\t(\fr12\frz9)}text)",
          "normalize rotation and preserve other rotations, nested transforms and block comments");
    check(setRotationZTag(chinese, -720.0) == R"({\frz0})" + chinese, "insert rotation without negative zero");
    check(setRotationZTag(chinese, -180) == R"({\frz-180})" + chinese &&
          setRotationZTag(chinese, 180) == R"({\frz180})" + chinese, "keep both closed interval endpoints");
    check(parseScale(R"({\t(\fscy2)\fscy inf\fscx +112.25})", &style, &x, &y)
          && x == 112.25 && y == 94.5, "use override and style fallback independently for each scale axis");
    check(parseScale(chinese, &style, &x, &y) && x == 106 && y == 94.5, "use both style scale defaults");
    check(!parseScale(chinese, nullptr, &x, &y) && x == 100 && y == 100, "use ASS default scale without a style");
    check(setScaleTags(R"({\i1\fscy122\t(\fscx40\fscy50)\fscx98}{\fscx99}text)" + chinese, 0.5, 113.26)
          == R"({\i1\fscy113.3\t(\fscx40\fscy50)\fscx1}text)" + chinese,
          "clamp and replace scale tags without touching transforms");
    check(setScaleTags(R"({\fscy102})" + chinese, -2, 93.5) == R"({\fscx1\fscy93.5})" + chinese,
          "insert missing scale axis");
    const std::string original = R"({note}{\pos(1.25,4)\move(-2,3,4,5, +010 , 20 )\org(6,7)\clip(-3,2,8,10)\iclip(1,2,3,4)\fs12.5 \fs+2\fs-1\fscx100}text{\t(0,20,\fs8\clip(2,4,6,8))\clip(m 0 0 l 1 1)})";
    check(resampleLineTags(original, 0.5, 2) ==
          R"({note}{\pos(0.625,8)\move(-1,6,2,10, +010 , 20 )\org(3,14)\clip(-2,4,4,20)\iclip(1,4,2,8)\fs25 \fs+2\fs-1\fscx100}text{\t(0,20,\fs16\clip(1,8,3,16))\clip(m 0 0 l 1 1)})",
          "nonuniform resampling preserves timing, relative sizes, drawings and transform syntax");
    check(resampleLineTags(original, 1, 1) == original && resampleLineTags(original, 0, 2) == original,
          "leave text unchanged for identity and invalid scales");
    const std::string malformed = R"(plain \pos(4,8){\pos(2,3,4)\fs1x\move(1,2,3,4,5)\iclip(1,2,,3)})";
    check(resampleLineTags(malformed, 2, 3) == malformed, "leave malformed tags and plain text unchanged");
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
