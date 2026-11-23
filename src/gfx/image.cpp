#include "gfx/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "gfx/font.h"

namespace kara {

uint32_t withAlpha(uint32_t c, int alpha) { return (c & 0x00FFFFFFu) | (uint32_t(std::clamp(alpha, 0, 255)) << 24); }

uint32_t shade(uint32_t c, float f) {
    auto ch = [f](uint32_t v) { return uint32_t(std::clamp(int(std::lround(float(v) * f)), 0, 255)); };
    return (c & 0xFF000000u) | (ch((c >> 16) & 255u) << 16) | (ch((c >> 8) & 255u) << 8) | ch(c & 255u);
}

uint32_t mixc(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [t](uint32_t x, uint32_t y) {
        return uint32_t(std::lround(float(x) + (float(y) - float(x)) * t));
    };
    return (ch(a >> 24, b >> 24) << 24) | (ch((a >> 16) & 255u, (b >> 16) & 255u) << 16) |
           (ch((a >> 8) & 255u, (b >> 8) & 255u) << 8) | ch(a & 255u, b & 255u);
}

Image::Image(int w, int h, uint32_t fill) : w_(w), h_(h), px_(size_t(w) * size_t(h), fill) {}

void Image::blend(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    const uint32_t a = c >> 24;
    if (a == 0) return;
    uint32_t& d = px_[size_t(y) * size_t(w_) + size_t(x)];
    if (a == 255) {
        d = c;
        return;
    }
    const uint32_t da = d >> 24;
    const uint32_t ia = 255 - a;
    auto ch = [&](int shift) {
        return ((((c >> shift) & 255u) * a + ((d >> shift) & 255u) * ia) + 127u) / 255u;
    };
    const uint32_t outA = a + (da * ia + 127u) / 255u;
    d = (outA << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

void Image::clear(uint32_t c) { std::fill(px_.begin(), px_.end(), c); }

void Image::fill(int x, int y, int w, int h, uint32_t c) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(w_, x + w), y1 = std::min(h_, y + h);
    if (x1 <= x0 || y1 <= y0) return;
    const bool solid = (c >> 24) == 255;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = &px_[size_t(yy) * size_t(w_)];
        for (int xx = x0; xx < x1; ++xx) {
            if (solid) row[xx] = c;
            else blend(xx, yy, c);
        }
    }
}

void Image::line(int x0, int y0, int x1, int y1, uint32_t c) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blend(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Image::disc(float cx, float cy, float r, uint32_t c) { capsule(Vec{cx, cy}, Vec{cx, cy}, r, c); }

void Image::ellipse(float cx, float cy, float rx, float ry, uint32_t c) {
    if (rx <= 0.f || ry <= 0.f) return;
    const int x0 = int(std::floor(cx - rx)), x1 = int(std::ceil(cx + rx));
    const int y0 = int(std::floor(cy - ry)), y1 = int(std::ceil(cy + ry));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = (float(x) + 0.5f - cx) / rx, dy = (float(y) + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.f) blend(x, y, c);
        }
}

void Image::capsule(Vec a, Vec b, float r, uint32_t c) {
    const int x0 = int(std::floor(std::min(a.x, b.x) - r)), x1 = int(std::ceil(std::max(a.x, b.x) + r));
    const int y0 = int(std::floor(std::min(a.y, b.y) - r)), y1 = int(std::ceil(std::max(a.y, b.y) + r));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (segmentDistance(Vec{float(x) + 0.5f, float(y) + 0.5f}, a, b) <= r) blend(x, y, c);
}

void Image::blit(const Image& src, int x, int y, bool flip, int alpha) {
    for (int sy = 0; sy < src.h_; ++sy)
        for (int sx = 0; sx < src.w_; ++sx) {
            uint32_t c = src.px_[size_t(sy) * size_t(src.w_) + size_t(flip ? src.w_ - 1 - sx : sx)];
            if ((c >> 24) == 0) continue;
            if (alpha < 255) c = withAlpha(c, int((c >> 24) * uint32_t(alpha) / 255u));
            blend(x + sx, y + sy, c);
        }
}

void Image::blitSolid(const Image& src, int x, int y, uint32_t c, bool flip) {
    for (int sy = 0; sy < src.h_; ++sy)
        for (int sx = 0; sx < src.w_; ++sx) {
            const uint32_t s = src.px_[size_t(sy) * size_t(src.w_) + size_t(flip ? src.w_ - 1 - sx : sx)];
            if ((s >> 24) >= 128) blend(x + sx, y + sy, c);
        }
}

int Image::textWidth(const std::string& s, int scale) {
    return s.empty() ? 0 : int(s.size()) * font::kAdvance * scale - scale;
}

int Image::text(const std::string& s, int x, int y, uint32_t c, int scale, int align) {
    const int w = textWidth(s, scale);
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    for (size_t i = 0; i < s.size(); ++i) {
        const char* const* rows = font::glyph(s[i]);
        if (!rows) continue;
        const int gx = x + int(i) * font::kAdvance * scale;
        for (int ry = 0; ry < font::kGlyphH; ++ry)
            for (int rx = 0; rx < font::kGlyphW; ++rx)
                if (rows[ry][rx] == '#') fill(gx + rx * scale, y + ry * scale, scale, scale, c);
    }
    return w;
}

int Image::textOutlined(const std::string& s, int x, int y, uint32_t c, uint32_t outline, int scale, int align) {
    const int w = textWidth(s, scale);
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    const int o = scale >= 3 ? 2 : 1;
    for (int dy = -o; dy <= o; ++dy)
        for (int dx = -o; dx <= o; ++dx)
            if (dx || dy) text(s, x + dx, y + dy, outline, scale);
    text(s, x, y + o, outline, scale);
    text(s, x, y, c, scale);
    return w;
}

}  // namespace kara
