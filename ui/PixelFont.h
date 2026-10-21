#pragma once
#include "imgui.h"
#include <string_view>
namespace lab {
// A 5 x 7 pixel font drawn as filled squares, so text stays crisp at any
// integer scale. ASCII letters in both cases, digits and symbols. Extra glyphs:
// \x01 cursor triangle, \x02 up, \x03 down, \x04 left, \x05 right arrow.
namespace pixelfont {
constexpr int glyphW=5,glyphH=7,advance=6;   // in font pixels
int width(std::string_view text);            // in font pixels
// Draws text with its top-left at p; one font pixel is `scale` screen pixels.
void draw(ImDrawList* d,ImVec2 p,float scale,ImU32 colour,std::string_view text);
// How a font pixel is drawn: 0 squares (the default), 1 circles, 2 diamonds,
// 3 outlined squares, 4 rounded squares, 5 squares with gaps. The recorded
// videos give each one its own.
void setStyle(int style);
}
}
