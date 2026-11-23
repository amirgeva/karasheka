#pragma once

// Built-in 5x7 pixel font (upper case, digits, punctuation, arrows), so the
// game needs no font files or SDL_ttf.
namespace kara::font {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kAdvance = 6;

// Arrow glyphs, usable inside string literals: "MOVE " KARA_ARROW_UP
#define KARA_ARROW_UP "\001"
#define KARA_ARROW_DOWN "\002"
#define KARA_ARROW_LEFT "\003"
#define KARA_ARROW_RIGHT "\004"
#define KARA_DEGREE "\005"

// Seven rows of five characters ('#' = lit) or nullptr if the glyph is blank
// or unknown. Lower case letters map to upper case.
const char* const* glyph(char c);

}  // namespace kara::font
