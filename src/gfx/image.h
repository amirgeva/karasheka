#pragma once

// A small software framebuffer. The whole game is drawn at 320 x 180 into
// one of these (crisp, unfiltered pixels, the way old home computers did it)
// and scaled up to the window at the end.

#include <cstdint>
#include <string>
#include <vector>

#include "core/pose.h"

namespace kara {

// Colours are ARGB; alpha 255 is opaque, 0 is transparent.
constexpr uint32_t argb(int r, int g, int b, int a = 255) {
    return (uint32_t(a & 255) << 24) | (uint32_t(r & 255) << 16) | (uint32_t(g & 255) << 8) | uint32_t(b & 255);
}
constexpr uint32_t opaque(uint32_t rgb) { return 0xFF000000u | rgb; }
inline int alphaOf(uint32_t c) { return int(c >> 24); }
uint32_t withAlpha(uint32_t c, int alpha);
uint32_t shade(uint32_t c, float f);             // scales r, g, b
uint32_t mixc(uint32_t a, uint32_t b, float t);  // blends a toward b

class Image {
public:
    Image() = default;
    Image(int w, int h, uint32_t fill = 0u);

    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return px_.empty(); }
    const uint32_t* data() const { return px_.data(); }
    uint32_t* data() { return px_.data(); }

    uint32_t get(int x, int y) const {
        return x >= 0 && y >= 0 && x < w_ && y < h_ ? px_[size_t(y) * size_t(w_) + size_t(x)] : 0u;
    }
    void set(int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < w_ && y < h_) px_[size_t(y) * size_t(w_) + size_t(x)] = c;
    }
    void blend(int x, int y, uint32_t c);  // source-over, any alpha

    void clear(uint32_t c);
    void fill(int x, int y, int w, int h, uint32_t c);  // blends when c is translucent
    void line(int x0, int y0, int x1, int y1, uint32_t c);
    void disc(float cx, float cy, float r, uint32_t c);
    void ellipse(float cx, float cy, float rx, float ry, uint32_t c);
    void capsule(Vec a, Vec b, float r, uint32_t c);
    void blit(const Image& src, int x, int y, bool flip = false, int alpha = 255);
    // Copies src with every opaque pixel replaced by `c` (silhouettes, flashes).
    void blitSolid(const Image& src, int x, int y, uint32_t c, bool flip = false);

    // The built-in 5x7 font. align: -1 left, 0 centre, 1 right. Returns the width.
    int text(const std::string& s, int x, int y, uint32_t c, int scale = 1, int align = -1);
    // Text with a one-pixel dark outline (per scaled pixel), for titles over busy art.
    int textOutlined(const std::string& s, int x, int y, uint32_t c, uint32_t outline, int scale = 1, int align = -1);
    static int textWidth(const std::string& s, int scale = 1);

private:
    int w_ = 0, h_ = 0;
    std::vector<uint32_t> px_;
};

}  // namespace kara
