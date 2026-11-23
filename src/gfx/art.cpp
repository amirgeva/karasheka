#include "gfx/art.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/rng.h"
#include "gfx/font.h"

namespace kara::art {
namespace {

constexpr uint32_t kInk = opaque(0x1E1619);  // outlines
constexpr uint32_t kWhite = opaque(0xFFFFFF);
const Vec kLight{-0.45f, -0.89f};              // toward the light: up and to the left
constexpr int kWallBottom = 141;               // wall rows 0..140, baseboard below
constexpr int kFloorTop = 148;                 // first floor row
constexpr float kVanishY = 60.f;               // the floor's vanishing point

struct Ramp {
    uint32_t hi, base, lo;
};

Ramp rampOf(uint32_t base, float hi = 1.16f, float lo = 0.76f) { return Ramp{shade(base, hi), base, shade(base, lo)}; }
Ramp darker(const Ramp& r, float f) { return Ramp{shade(r.hi, f), shade(r.base, f), shade(r.lo, f)}; }

struct Look {
    uint32_t gi, skin, hair, belt, band;
};

const Look kLookTable[kLooks] = {
    {opaque(0xF1EEE6), opaque(0xE3A97F), opaque(0x2A1D17), opaque(0x1C1818), opaque(0xD9302E)},
    {opaque(0x34458A), opaque(0xC68A5E), opaque(0x5B3419), opaque(0x8A5A2B), opaque(0xF2C230)},
};

Vec norm(Vec v) {
    const float l = length(v);
    return l > 1e-4f ? v * (1.f / l) : Vec{0.f, -1.f};
}

Vec closest(Vec p, Vec a, Vec b) {
    const Vec ab = b - a;
    const float len2 = dot(ab, ab);
    const float t = len2 > 1e-6f ? clamp01(dot(p - a, ab) / len2) : 0.f;
    return a + ab * t;
}

// A limb with cylinder shading: lit along its upper-left side.
void fillPart(Image& img, Vec a, Vec b, float r, const Ramp& rp) {
    const int x0 = int(std::floor(std::min(a.x, b.x) - r)), x1 = int(std::ceil(std::max(a.x, b.x) + r));
    const int y0 = int(std::floor(std::min(a.y, b.y) - r)), y1 = int(std::ceil(std::max(a.y, b.y) + r));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const Vec p{float(x) + 0.5f, float(y) + 0.5f};
            const Vec q = closest(p, a, b);
            const Vec d = p - q;
            const float dist = length(d);
            if (dist > r) continue;
            const float l = dot(d, kLight) / std::max(r, 0.8f);
            img.set(x, y, l > 0.38f ? rp.hi : (l < -0.32f ? rp.lo : rp.base));
        }
}

struct Part {
    Vec a, b;
    float r;
    Ramp ramp;
};

// Outlines first, then fills, so a limb has no seams at its own joints but
// stands out against whatever it overlaps.
void drawGroup(Image& img, const Part* parts, int n) {
    for (int i = 0; i < n; ++i) img.capsule(parts[i].a, parts[i].b, parts[i].r + 1.f, kInk);
    for (int i = 0; i < n; ++i) fillPart(img, parts[i].a, parts[i].b, parts[i].r, parts[i].ramp);
}

uint32_t hashu(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float hash01(int x, int y, uint32_t seed) { return float(hashu(x, y, seed) & 0xFFFFu) / 65535.f; }

// A filled box with a lit top-left edge and a shaded bottom-right edge.
void box(Image& img, int x, int y, int w, int h, uint32_t base) {
    if (w <= 0 || h <= 0) return;
    img.fill(x, y, w, h, base);
    img.fill(x, y, w, 1, shade(base, 1.18f));
    img.fill(x, y, 1, h, shade(base, 1.08f));
    img.fill(x, y + h - 1, w, 1, shade(base, 0.72f));
    img.fill(x + w - 1, y, 1, h, shade(base, 0.82f));
}

void border(Image& img, int x, int y, int w, int h, uint32_t c) {
    img.fill(x, y, w, 1, c);
    img.fill(x, y + h - 1, w, 1, c);
    img.fill(x, y, 1, h, c);
    img.fill(x + w - 1, y, 1, h, c);
}

void vgrad(Image& img, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    for (int i = 0; i < h; ++i) img.fill(x, y + i, w, 1, mixc(top, bottom, h > 1 ? float(i) / float(h - 1) : 0.f));
}

// A soft pool of light (translucent, additive-looking).
void glow(Image& img, float cx, float cy, float r, uint32_t color, float strength) {
    const int x0 = int(cx - r), x1 = int(cx + r), y0 = int(cy - r), y1 = int(cy + r);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = (float(x) - cx) / r, dy = (float(y) - cy) / r;
            const float d = dx * dx + dy * dy;
            if (d >= 1.f) continue;
            const float a = (1.f - d) * (1.f - d) * strength;
            // Ordered dither keeps the pixel look.
            const float bayer = float(((x & 1) * 2 + (y & 1) * 3) % 4) / 4.f;
            img.blend(x, y, withAlpha(color, int(std::clamp(a * 255.f + (bayer - 0.4f) * 18.f, 0.f, 255.f))));
        }
}

// Turns every transparent pixel that touches an opaque one into ink.
void outlineSprite(Image& img) {
    const int w = img.width(), h = img.height();
    std::vector<uint8_t> solid(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) solid[size_t(y * w + x)] = alphaOf(img.get(x, y)) >= 128;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (solid[size_t(y * w + x)]) continue;
            const bool edge = (x > 0 && solid[size_t(y * w + x - 1)]) || (x + 1 < w && solid[size_t(y * w + x + 1)]) ||
                              (y > 0 && solid[size_t((y - 1) * w + x)]) || (y + 1 < h && solid[size_t((y + 1) * w + x)]);
            if (edge) img.set(x, y, kInk);
        }
}

// ---------------------------------------------------------------- room parts

void windowPane(Image& img, int x, int y, int w, int h, int kind) {
    const uint32_t frame = opaque(0xE9E3D6);
    // Sky.
    const int gx = x + 3, gy = y + 3, gw = w - 6, gh = h - 6;
    switch (kind) {
        case 0:  // night city
            vgrad(img, gx, gy, gw, gh, opaque(0x0B1230), opaque(0x2B2F5E));
            break;
        case 1:  // day
            vgrad(img, gx, gy, gw, gh, opaque(0x4F9BE0), opaque(0xBFE3F7));
            break;
        case 2:  // moonlit
            vgrad(img, gx, gy, gw, gh, opaque(0x101836), opaque(0x324472));
            break;
        default:  // dusk
            vgrad(img, gx, gy, gw, gh, opaque(0x3A2D68), opaque(0xF09A5A));
            break;
    }
    Rng rng(uint64_t(kind) * 977u + uint64_t(x));
    if (kind == 0 || kind == 2) {
        for (int i = 0; i < gw * gh / 40; ++i)
            img.set(gx + int(rng.below(uint32_t(gw))), gy + int(rng.below(uint32_t(gh * 2 / 3))),
                    rng.uniform() < 0.3f ? opaque(0xFFFFFF) : opaque(0x9AA6D8));
    }
    if (kind == 2) {
        const float mx = float(gx + gw * 3 / 4), my = float(gy + gh / 4);
        glow(img, mx, my, 12.f, opaque(0xC8D4FF), 0.35f);
        img.disc(mx, my, 5.f, opaque(0xF2F0DC));
        img.disc(mx + 2.f, my - 1.f, 4.f, opaque(0x18224A));  // crescent
    }
    if (kind == 1) {
        for (int c = 0; c < 3; ++c) {
            const float cx = float(gx) + rng.uniform(4.f, float(gw) - 4.f), cy = float(gy) + rng.uniform(4.f, float(gh) * 0.5f);
            for (int k = 0; k < 4; ++k)
                img.disc(cx + float(k) * 3.f - 4.f, cy + (k == 1 || k == 2 ? -1.5f : 0.f), 2.6f, opaque(0xF7FBFF));
        }
        // Distant tree tops.
        for (int xx = gx; xx < gx + gw; ++xx) {
            const int top = gy + gh - 6 - int(3.f * std::sin(float(xx) * 0.45f) + 2.f * std::sin(float(xx) * 1.3f));
            img.fill(xx, top, 1, gy + gh - top, opaque(0x4F8A4A));
        }
    }
    if (kind == 0 || kind == 3) {
        // Skyline with lit windows.
        int bx = gx;
        while (bx < gx + gw) {
            const int bw = 5 + int(rng.below(8));
            const int bh = gh / 4 + int(rng.below(uint32_t(gh / 2)));
            const uint32_t body = kind == 0 ? opaque(0x161A33) : opaque(0x2A2140);
            img.fill(bx, gy + gh - bh, std::min(bw, gx + gw - bx), bh, body);
            for (int wy = gy + gh - bh + 2; wy < gy + gh - 1; wy += 3)
                for (int wx = bx + 1; wx < bx + bw - 1 && wx < gx + gw; wx += 2)
                    if (rng.uniform() < 0.33f) img.set(wx, wy, kind == 0 ? opaque(0xF6D36B) : opaque(0xFFC27A));
            bx += bw + 1;
        }
    }
    // Glass sheen, frame and bars.
    for (int i = 0; i < gh; ++i) {
        const int sx = gx + gw / 5 + (gh - i) / 2;
        if (sx >= gx && sx < gx + gw) img.blend(sx, gy + i, argb(255, 255, 255, 40));
        if (sx + 2 >= gx && sx + 2 < gx + gw) img.blend(sx + 2, gy + i, argb(255, 255, 255, 28));
    }
    border(img, x, y, w, h, kInk);
    img.fill(x + 1, y + 1, w - 2, 2, frame);
    img.fill(x + 1, y + h - 3, w - 2, 2, frame);
    img.fill(x + 1, y + 1, 2, h - 2, frame);
    img.fill(x + w - 3, y + 1, 2, h - 2, frame);
    img.fill(x + w / 2 - 1, y + 2, 2, h - 4, frame);
    img.fill(x + 2, y + h * 9 / 20, w - 4, 2, frame);
    // Sill.
    img.fill(x - 3, y + h, w + 6, 3, shade(frame, 0.95f));
    img.fill(x - 3, y + h + 3, w + 6, 1, shade(frame, 0.6f));
}

void curtain(Image& img, int x, int y, int w, int h, uint32_t base) {
    for (int i = 0; i < w; ++i) {
        const float f = 0.82f + 0.22f * std::sin(float(i) * 1.6f);
        const int sway = int(std::lround(std::sin(float(i) * 0.7f) * 1.f));
        img.fill(x + i, y, 1, h + sway, shade(base, f));
    }
    img.fill(x - 1, y - 2, w + 2, 2, opaque(0x6B4A2E));  // rail
}

void picture(Image& img, int x, int y, int w, int h, int kind) {
    const uint32_t frame = kind == 2 ? opaque(0x222222) : opaque(0xB38A3E);
    img.fill(x, y, w, h, kInk);
    img.fill(x + 1, y + 1, w - 2, h - 2, frame);
    img.fill(x + 1, y + 1, w - 2, 1, shade(frame, 1.25f));
    const int ix = x + 3, iy = y + 3, iw = w - 6, ih = h - 6;
    switch (kind) {
        case 0: {  // a sunset over hills
            vgrad(img, ix, iy, iw, ih, opaque(0xF4C46A), opaque(0xE0704A));
            img.disc(float(ix + iw * 2 / 3), float(iy + ih / 2), float(ih) / 4.f, opaque(0xFFF0B0));
            for (int xx = 0; xx < iw; ++xx) {
                const int top = ih * 2 / 3 + int(3.f * std::sin(float(xx) * 0.25f));
                img.fill(ix + xx, iy + top, 1, ih - top, opaque(0x5A3B5E));
            }
            break;
        }
        case 1: {  // flowers in a vase
            img.fill(ix, iy, iw, ih, opaque(0xE8E0CC));
            const float vx = float(ix + iw / 2);
            img.fill(int(vx) - 3, iy + ih - 8, 6, 8, opaque(0x3E6FA8));
            for (int k = 0; k < 5; ++k) {
                const float fx = vx - 6.f + float(k) * 3.f, fy = float(iy) + 4.f + float(k % 2) * 3.f;
                img.line(int(vx), iy + ih - 8, int(fx), int(fy), opaque(0x4E8A3A));
                img.disc(fx, fy, 1.8f, k % 2 ? opaque(0xE0524A) : opaque(0xF2C230));
            }
            break;
        }
        case 2: {  // dojo poster
            img.fill(ix, iy, iw, ih, opaque(0xF0E6CC));
            img.disc(float(ix + iw / 2), float(iy + ih / 2 - 3), float(std::min(iw, ih)) / 3.f, opaque(0xD9302E));
            img.text("DOJO", ix + iw / 2, iy + ih - 9, opaque(0x1E1619), 1, 0);
            break;
        }
        default: {  // certificate
            img.fill(ix, iy, iw, ih, opaque(0xF4EEDC));
            for (int l = 0; l < ih - 6; l += 3) img.fill(ix + 2, iy + 3 + l, iw - 4 - (l % 2) * 3, 1, opaque(0x8E8574));
            img.disc(float(ix + iw - 4), float(iy + ih - 4), 2.f, opaque(0xC0392B));
            break;
        }
    }
}

void wallClock(Image& img, float cx, float cy, float r) {
    img.disc(cx, cy, r + 1.f, kInk);
    img.disc(cx, cy, r, opaque(0xB9853E));
    img.disc(cx, cy, r - 1.5f, opaque(0xF7F2E4));
    for (int i = 0; i < 12; i += 3) {
        const float a = float(i) * 0.5236f;
        img.set(int(cx + std::sin(a) * (r - 2.5f)), int(cy - std::cos(a) * (r - 2.5f)), kInk);
    }
    img.line(int(cx), int(cy), int(cx), int(cy - r + 3.f), kInk);
    img.line(int(cx), int(cy), int(cx + r - 4.f), int(cy + 1.f), kInk);
}

void plant(Image& img, int x, int baseY) {
    for (int i = 0; i < 9; ++i) {
        const float a = -1.3f + float(i) * 0.33f;
        const Vec root{float(x) + 5.f, float(baseY) - 10.f};
        const Vec tip = root + Vec{std::sin(a) * 11.f, -std::cos(a) * 13.f + std::fabs(a) * 3.f};
        img.capsule(root, tip, 2.0f, kInk);
    }
    for (int i = 0; i < 9; ++i) {
        const float a = -1.3f + float(i) * 0.33f;
        const Vec root{float(x) + 5.f, float(baseY) - 10.f};
        const Vec tip = root + Vec{std::sin(a) * 11.f, -std::cos(a) * 13.f + std::fabs(a) * 3.f};
        img.capsule(root, tip, 1.1f, i % 2 ? opaque(0x3F8A3C) : opaque(0x5DAA4A));
    }
    // Pot.
    for (int r = 0; r < 10; ++r) img.fill(x + r / 4, baseY - 10 + r, 11 - r / 2, 1, opaque(0xB8603A));
    img.fill(x - 1, baseY - 11, 13, 2, opaque(0xCF7448));
    border(img, x - 1, baseY - 11, 13, 2, kInk);
}

void floorLamp(Image& img, int x, int baseY) {
    glow(img, float(x), float(baseY - 50), 30.f, opaque(0xFFE0A0), 0.32f);
    img.fill(x - 4, baseY - 2, 9, 2, opaque(0x2A2A2A));
    img.fill(x, baseY - 46, 1, 44, opaque(0x3A3A3A));
    for (int r = 0; r < 10; ++r) img.fill(x - 4 - r / 3, baseY - 58 + r, 9 + (r / 3) * 2, 1, shade(opaque(0xF3D9A0), 1.f - 0.02f * float(r)));
    border(img, x - 8, baseY - 59, 17, 12, kInk);
}

void deskLamp(Image& img, int x, int baseY) {
    glow(img, float(x + 8), float(baseY - 8), 22.f, opaque(0xFFE6A8), 0.35f);
    img.fill(x - 3, baseY - 2, 8, 2, opaque(0x2F5C3A));
    img.line(x, baseY - 2, x + 3, baseY - 14, opaque(0x2F2F2F));
    img.line(x + 3, baseY - 14, x + 9, baseY - 18, opaque(0x2F2F2F));
    img.fill(x + 7, baseY - 20, 7, 4, opaque(0x2F5C3A));
    img.fill(x + 8, baseY - 16, 5, 1, opaque(0xFFF2C0));
}

// ---------------------------------------------------------------- floors

void floorPlanks(Image& img, uint32_t base, uint32_t seamColor, float lane) {
    for (int y = kFloorTop; y < kWorldH; ++y) {
        const float d = float(y) + 0.5f - kVanishY;
        const float depth = 1.f - (float(kWorldH - y) / float(kWorldH - kFloorTop)) * 0.22f;
        for (int x = 0; x < kWorldW; ++x) {
            const float u = (float(x) + 0.5f - 160.f) / d / lane;
            const float fu = u - std::floor(u);
            const int ln = int(std::floor(u)) + 100;
            uint32_t c = shade(base, (0.9f + 0.16f * hash01(ln, 7, 3u)) * depth);
            const float z = 1400.f / d / 3.f + hash01(ln, 3, 9u) * 4.f;
            const float fz = z - std::floor(z);
            if (fu * lane * d < 1.f || fz < 0.035f) c = seamColor;
            else if (hash01(x, y, 5u) < 0.12f) c = shade(c, 0.93f);
            img.set(x, y, c);
        }
    }
}

void floorChecker(Image& img, uint32_t a, uint32_t b) {
    for (int y = kFloorTop; y < kWorldH; ++y) {
        const float d = float(y) + 0.5f - kVanishY;
        for (int x = 0; x < kWorldW; ++x) {
            const float u = (float(x) + 0.5f - 160.f) / d / 0.2f;
            const float z = 1500.f / d;
            const int tx = int(std::floor(u)) + 100, tz = int(std::floor(z));
            uint32_t c = ((tx + tz) & 1) ? a : b;
            const float fu = u - std::floor(u);
            if (fu * 0.2f * d < 0.9f) c = shade(c, 0.82f);
            img.set(x, y, shade(c, 0.86f + 0.14f * float(y - kFloorTop) / float(kWorldH - kFloorTop)));
        }
    }
}

void floorCarpet(Image& img, uint32_t a, uint32_t b) {
    for (int y = kFloorTop; y < kWorldH; ++y)
        for (int x = 0; x < kWorldW; ++x) {
            const float n = hash01(x, y, 11u) * 0.6f + hash01(x / 2, y / 2, 12u) * 0.4f;
            uint32_t c = n > 0.62f ? b : a;
            img.set(x, y, shade(c, 0.86f + 0.14f * float(y - kFloorTop) / float(kWorldH - kFloorTop)));
        }
}

void rug(Image& img, float halfU, int y0, int y1, uint32_t base, uint32_t edge) {
    for (int y = y0; y < y1; ++y) {
        const float d = float(y) + 0.5f - kVanishY;
        for (int x = 0; x < kWorldW; ++x) {
            const float u = (float(x) + 0.5f - 160.f) / d;
            if (std::fabs(u) > halfU) continue;
            const bool rim = std::fabs(u) > halfU - 2.5f / d * 1.f || y < y0 + 2 || y >= y1 - 2;
            const bool inner = std::fabs(u) < halfU * 0.6f && y > y0 + 4 && y < y1 - 4;
            uint32_t c = rim ? edge : base;
            if (inner && ((x + y) % 4 == 0)) c = shade(edge, 1.1f);
            img.set(x, y, c);
        }
    }
}

// ---------------------------------------------------------------- walls

void wallStripes(Image& img, uint32_t a, uint32_t b) {
    for (int y = 0; y < kWallBottom; ++y)
        for (int x = 0; x < kWorldW; ++x) img.set(x, y, (x / 6) % 2 ? a : b);
}

void wallDiamonds(Image& img, uint32_t base, uint32_t dot) {
    for (int y = 0; y < kWallBottom; ++y)
        for (int x = 0; x < kWorldW; ++x) {
            const int u = (x + y) % 12, v = (x - y + 1200) % 12;
            img.set(x, y, (u == 0 || v == 0) && ((x / 6 + y / 6) % 2 == 0) ? dot : base);
        }
}

void wallPlain(Image& img, uint32_t base) {
    for (int y = 0; y < kWallBottom; ++y)
        for (int x = 0; x < kWorldW; ++x) img.set(x, y, hash01(x, y, 21u) < 0.06f ? shade(base, 0.96f) : base);
}

void wainscot(Image& img, int top, uint32_t wood) {
    img.fill(0, top, kWorldW, kWallBottom - top, wood);
    img.fill(0, top, kWorldW, 2, shade(wood, 1.25f));
    img.fill(0, top + 2, kWorldW, 1, shade(wood, 0.6f));
    for (int x = 4; x < kWorldW - 20; x += 26) {
        border(img, x, top + 6, 20, kWallBottom - top - 10, shade(wood, 0.72f));
        border(img, x + 1, top + 7, 18, kWallBottom - top - 12, shade(wood, 1.12f));
    }
}

void baseboard(Image& img, uint32_t trim) {
    img.fill(0, kWallBottom, kWorldW, kFloorTop - kWallBottom, trim);
    img.fill(0, kWallBottom, kWorldW, 1, shade(trim, 1.2f));
    img.fill(0, kFloorTop - 1, kWorldW, 1, shade(trim, 0.55f));
}

void ceilingShadow(Image& img) {
    for (int y = 0; y < 34; ++y) {
        const float f = 0.62f + 0.38f * float(y) / 34.f;
        for (int x = 0; x < kWorldW; ++x) img.set(x, y, shade(img.get(x, y), f));
    }
    // Darker corners.
    for (int y = 0; y < kWorldH; ++y)
        for (int x = 0; x < 10; ++x) {
            const float f = 0.7f + 0.3f * float(x) / 10.f;
            img.set(x, y, shade(img.get(x, y), f));
            img.set(kWorldW - 1 - x, y, shade(img.get(kWorldW - 1 - x, y), f));
        }
}

}  // namespace

uint32_t playerColor(int fighter) { return kLookTable[size_t(std::clamp(fighter, 0, kLooks - 1))].band; }

// ---------------------------------------------------------------- fighters

void drawFighter(Image& img, const Skeleton& s, int look, bool flash, float time) {
    const Look& lk = kLookTable[size_t(std::clamp(look, 0, kLooks - 1))];
    Ramp gi = rampOf(lk.gi), skin = rampOf(lk.skin), belt = rampOf(lk.belt, 1.5f, 0.7f);
    Ramp band = rampOf(lk.band), hair = rampOf(lk.hair, 1.35f, 0.8f);
    if (flash) gi = skin = belt = band = hair = Ramp{kWhite, kWhite, opaque(0xD8D8E8)};
    const float fs = float(s.facing);

    auto arm = [&](int i, float dim) {
        const Ramp g = darker(gi, dim), k = darker(skin, dim);
        const Vec fdir = norm(s.hand[i] - s.elbow[i]);
        const Vec sleeve = s.elbow[i] + (s.hand[i] - s.elbow[i]) * 0.5f;
        const Vec fist = s.hand[i] + fdir * 0.6f;
        const Part parts[] = {
            {s.shoulder, s.elbow[i], 2.6f, g},
            {s.elbow[i], s.hand[i], 2.0f, k},
            {s.elbow[i], sleeve, 2.5f, g},
            {fist, fist, 2.4f, k},
        };
        drawGroup(img, parts, 4);
    };
    auto leg = [&](int i, float dim) {
        const Ramp g = darker(gi, dim), k = darker(skin, dim);
        const Part parts[] = {
            {s.hip, s.knee[i], 3.6f, g},
            {s.knee[i], s.ankle[i], 3.0f, g},
            {s.ankle[i], s.toe[i], 1.5f, k},
        };
        drawGroup(img, parts, 3);
    };

    const Vec tdir = norm(s.shoulder - s.hip);
    Vec perp{-tdir.y, tdir.x};
    if (perp.x * fs < 0.f || (perp.x == 0.f && perp.y > 0.f)) perp = perp * -1.f;  // toward the front

    // Headband tails, behind everything.
    const Vec up = s.up;
    const Vec fwd = Vec{-up.y, up.x} * fs;
    const Vec knot = s.head - fwd * 4.3f + up * 1.4f;
    const float w1 = std::sin(time * 11.f), w2 = std::sin(time * 11.f + 1.3f);
    const Part tails[] = {
        {knot, knot + Vec{-fs * 6.5f, 1.5f + w1 * 1.4f}, 0.9f, band},
        {knot, knot + Vec{-fs * 5.f, 4.f + w2 * 1.4f}, 0.9f, band},
    };
    drawGroup(img, tails, 2);

    arm(kBack, 0.8f);
    leg(kBack, 0.8f);

    const Part torso[] = {
        {s.hip, s.hip - tdir * 4.5f, 5.4f, gi},
        {s.hip, s.shoulder, 5.4f, gi},
    };
    drawGroup(img, torso, 2);
    // Lapel and belt.
    const Vec beltC = s.hip + tdir * 2.0f;
    const Vec neckFront = s.shoulder + perp * 1.0f;
    const Vec lapelEnd = beltC + tdir * 1.3f + perp * 4.6f;
    img.line(int(std::floor(neckFront.x)), int(std::floor(neckFront.y)), int(std::floor(lapelEnd.x)),
             int(std::floor(lapelEnd.y)), gi.lo);
    fillPart(img, beltC - perp * 5.4f, beltC + perp * 5.4f, 1.1f, belt);
    const Vec beltKnot = beltC + perp * 4.0f;
    fillPart(img, beltKnot, beltKnot, 1.5f, belt);
    img.line(int(beltKnot.x), int(beltKnot.y), int(beltKnot.x + fs * 1.f), int(beltKnot.y + 5.f), belt.base);
    img.line(int(beltKnot.x), int(beltKnot.y), int(beltKnot.x + fs * 2.5f), int(beltKnot.y + 4.f), belt.lo);

    leg(kFront, 1.f);

    // Neck and head.
    img.capsule(s.shoulder, s.neck, 3.2f, kInk);
    img.disc(s.head.x, s.head.y, kHeadR + 1.f, kInk);
    fillPart(img, s.shoulder, s.neck, 2.2f, skin);
    {
        const int x0 = int(std::floor(s.head.x - kHeadR)), x1 = int(std::ceil(s.head.x + kHeadR));
        const int y0 = int(std::floor(s.head.y - kHeadR)), y1 = int(std::ceil(s.head.y + kHeadR));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const Vec d = Vec{float(x) + 0.5f, float(y) + 0.5f} - s.head;
                if (length(d) > kHeadR) continue;
                const Vec n = d * (1.f / kHeadR);
                const float u = dot(n, up), f = dot(n, fwd), l = dot(n, kLight);
                const Ramp* rp = &skin;
                if (u > 0.42f || (f < -0.32f && u > -0.6f)) rp = &hair;
                if (u > 0.08f && u <= 0.42f) rp = &band;
                img.set(x, y, l > 0.4f ? rp->hi : (l < -0.35f ? rp->lo : rp->base));
            }
        const Vec eye = s.head + fwd * 2.7f - up * 0.4f;
        img.set(int(std::floor(eye.x)), int(std::floor(eye.y)), flash ? opaque(0x8888AA) : kInk);
    }

    arm(kFront, 1.f);
}

// ---------------------------------------------------------------- rooms

void paintRoom(Image& img, int room) {
    room = ((room % kRoomCount) + kRoomCount) % kRoomCount;
    img.clear(opaque(0x000000));
    switch (room) {
        case 0: {  // living room
            wallStripes(img, opaque(0xD9C49A), opaque(0xCFB88C));
            ceilingShadow(img);
            windowPane(img, 128, 50, 54, 58, 0);
            curtain(img, 118, 46, 11, 74, opaque(0xA8443A));
            curtain(img, 181, 46, 11, 74, opaque(0xA8443A));
            picture(img, 64, 74, 34, 24, 0);
            picture(img, 220, 82, 20, 22, 1);
            baseboard(img, opaque(0x7A5232));
            floorPlanks(img, opaque(0xB07A44), opaque(0x6E4527), 0.13f);
            rug(img, 0.62f, 153, 176, opaque(0x8E2F33), opaque(0xD9A74A));
            floorLamp(img, 202, 166);
            plant(img, 248, 166);
            break;
        }
        case 1: {  // kitchen
            wallPlain(img, opaque(0xA9CBB6));
            ceilingShadow(img);
            // Backsplash tiles behind the counter.
            for (int y = 100; y < kWallBottom; ++y)
                for (int x = 38; x < 100; ++x)
                    img.set(x, y, (x % 6 == 0 || y % 6 == 0) ? opaque(0xBFC6C3) : opaque(0xF1F1EA));
            // Upper cabinets.
            for (int i = 0; i < 3; ++i) {
                box(img, 40 + i * 20, 62, 19, 30, opaque(0xD8C9A8));
                border(img, 40 + i * 20, 62, 19, 30, kInk);
                img.fill(40 + i * 20 + (i == 1 ? 3 : 14), 84, 2, 4, opaque(0x6A6A6A));
            }
            windowPane(img, 146, 50, 48, 46, 1);
            curtain(img, 146, 74, 48, 22, opaque(0xE8E0C8));
            wallClock(img, 240.f, 70.f, 8.f);
            // A shelf of jars.
            img.fill(250, 108, 44, 3, opaque(0x8A5A32));
            border(img, 250, 108, 44, 3, kInk);
            const uint32_t jars[4] = {opaque(0xD9302E), opaque(0xF2C230), opaque(0x6BA34A), opaque(0x3E6FA8)};
            for (int i = 0; i < 4; ++i) {
                box(img, 253 + i * 10, 98, 7, 10, jars[i]);
                border(img, 253 + i * 10, 98, 7, 10, kInk);
                img.fill(254 + i * 10, 97, 5, 2, opaque(0xC8C8C8));
            }
            baseboard(img, opaque(0xE9E5DA));
            floorChecker(img, opaque(0xEDE8DA), opaque(0x2E3138));
            break;
        }
        case 2: {  // bedroom
            wallDiamonds(img, opaque(0x7C8FB8), opaque(0x91A3CA));
            ceilingShadow(img);
            windowPane(img, 156, 50, 52, 56, 2);
            curtain(img, 146, 46, 11, 72, opaque(0xD6C7E8));
            curtain(img, 207, 46, 11, 72, opaque(0xD6C7E8));
            picture(img, 72, 66, 28, 38, 2);
            // A mirror over the dresser.
            img.ellipse(258.f, 98.f, 11.f, 16.f, kInk);
            img.ellipse(258.f, 98.f, 10.f, 15.f, opaque(0xB9853E));
            img.ellipse(258.f, 98.f, 8.f, 13.f, opaque(0x9FB6D6));
            img.line(253, 92, 257, 86, opaque(0xE8F0FF));
            img.line(254, 96, 260, 87, opaque(0xD0DEF2));
            // A lamp on the nightstand.
            glow(img, 137.f, 136.f, 22.f, opaque(0xFFE0A0), 0.35f);
            img.fill(136, 140, 3, 9, opaque(0xB9853E));
            for (int r = 0; r < 7; ++r) img.fill(132 - r / 3, 133 + r, 11 + (r / 3) * 2, 1, opaque(0xF6E3B4));
            border(img, 130, 132, 15, 9, kInk);
            baseboard(img, opaque(0xF0ECE4));
            floorCarpet(img, opaque(0x8C7A6A), opaque(0x9C8A78));
            break;
        }
        default: {  // study
            wallPlain(img, opaque(0x5F7D5A));
            wainscot(img, 108, opaque(0x6E4529));
            ceilingShadow(img);
            windowPane(img, 128, 46, 52, 54, 3);
            picture(img, 194, 70, 18, 22, 3);
            picture(img, 216, 74, 18, 22, 3);
            wallClock(img, 262.f, 46.f, 7.f);
            deskLamp(img, 52, 144);
            baseboard(img, opaque(0x4A2E1A));
            floorPlanks(img, opaque(0x7A4B2A), opaque(0x3E2414), 0.11f);
            plant(img, 262, 166);
            break;
        }
    }
}

// ---------------------------------------------------------------- furniture

Image pieceSprite(Piece p) {
    const PieceSpec& sp = pieceSpec(p);
    Image img(sp.w + 2, sp.h + 2);
    // Sprite coordinates: (0, 0) is the piece's top-left, y grows down; the
    // floor is at y = h.
    const int W = sp.w, H = sp.h;
    auto R = [&](int x, int y, int w, int h, uint32_t c) { box(img, x + 1, y + 1, w, h, c); };
    auto F = [&](int x, int y, int w, int h, uint32_t c) { img.fill(x + 1, y + 1, w, h, c); };
    const int seatY = H - sp.top;  // y of the standable surface
    const uint32_t oak = opaque(0xA06C3C), walnut = opaque(0x5E3A22), pine = opaque(0xC8955A);
    switch (p) {
        case Piece::Chair: {
            R(0, 0, 3, H, oak);                 // back post down to the floor
            R(0, 3, 3, 3, shade(oak, 1.1f));
            for (int y = 8; y < seatY - 1; y += 4) F(3, y, 1, 2, oak);  // slats
            R(0, seatY, W, 3, oak);             // seat
            R(W - 3, seatY + 3, 2, H - seatY - 3, oak);
            break;
        }
        case Piece::Stool: {
            R(0, 0, W, 3, pine);
            img.line(3, 4, 1, H, shade(pine, 0.8f));
            img.line(W - 2, 4, W, H, shade(pine, 0.8f));
            img.line(5, 4, 5, H, pine);
            img.line(8, 4, 8, H, pine);
            F(2, H - 7, W - 4, 1, shade(pine, 0.7f));
            break;
        }
        case Piece::Table: {
            R(0, 0, W, 4, oak);
            R(2, 4, W - 4, 3, shade(oak, 0.85f));
            R(3, 7, 3, H - 7, shade(oak, 0.9f));
            R(W - 6, 7, 3, H - 7, shade(oak, 0.9f));
            break;
        }
        case Piece::CoffeeTable: {
            R(0, 0, W, 3, walnut);
            R(3, 3, 3, H - 3, walnut);
            R(W - 6, 3, 3, H - 3, walnut);
            R(5, H - 4, W - 10, 2, shade(walnut, 1.15f));  // under-shelf
            F(10, H - 6, 6, 2, opaque(0xD9302E));           // a book on it
            F(18, H - 6, 5, 2, opaque(0x3E6FA8));
            break;
        }
        case Piece::Sofa: {
            const uint32_t fabric = opaque(0x3E7F8A);
            R(4, 0, W - 8, 15, shade(fabric, 0.85f));     // back cushions
            F(W / 2, 1, 1, 13, shade(fabric, 0.65f));
            R(0, 8, 7, H - 11, fabric);                   // arms
            R(W - 7, 8, 7, H - 11, fabric);
            R(6, seatY, W - 12, 5, shade(fabric, 1.08f)); // seat cushions
            F(W / 2, seatY + 1, 1, 4, shade(fabric, 0.7f));
            R(5, seatY + 5, W - 10, H - seatY - 8, shade(fabric, 0.9f));
            F(2, H - 3, 3, 3, walnut);
            F(W - 5, H - 3, 3, 3, walnut);
            break;
        }
        case Piece::Armchair: {
            const uint32_t fabric = opaque(0xC08A2E);
            R(3, 0, W - 6, 15, shade(fabric, 0.85f));
            R(0, 7, 6, H - 10, fabric);
            R(W - 6, 7, 6, H - 10, fabric);
            R(5, seatY, W - 10, 5, shade(fabric, 1.08f));
            R(4, seatY + 5, W - 8, H - seatY - 8, shade(fabric, 0.9f));
            F(2, H - 3, 3, 3, walnut);
            F(W - 5, H - 3, 3, 3, walnut);
            break;
        }
        case Piece::TvCabinet: {
            R(0, 0, W, H - 2, walnut);
            R(2, 3, W / 2 - 3, H - 7, shade(walnut, 1.12f));
            R(W / 2 + 1, 3, W / 2 - 3, H - 7, shade(walnut, 1.12f));
            F(W / 2 - 3, H / 2, 1, 2, opaque(0xD8C080));
            F(W / 2 + 2, H / 2, 1, 2, opaque(0xD8C080));
            F(2, H - 2, 3, 2, walnut);
            F(W - 5, H - 2, 3, 2, walnut);
            break;
        }
        case Piece::Tv: {
            R(0, 0, W, H - 1, opaque(0x3A3634));
            R(2, 2, W - 9, H - 5, opaque(0x2B3A3A));
            for (int y = 3; y < H - 4; y += 2) F(3, y, W - 11, 1, opaque(0x5C8C8A));  // scanlines
            F(4, 4, 4, 2, opaque(0xB8F0E8));
            F(W - 5, 4, 2, 2, opaque(0xC8C8C8));  // knobs
            F(W - 5, 8, 2, 2, opaque(0xC8C8C8));
            F(W - 6, H - 6, 3, 1, opaque(0xD94040));
            F(4, H - 1, 4, 1, opaque(0x222222));
            F(W - 8, H - 1, 4, 1, opaque(0x222222));
            break;
        }
        case Piece::Counter: {
            const uint32_t door = opaque(0x8FB0C8);
            R(0, 0, W, 4, opaque(0xD8D4CC));  // stone top
            R(1, 4, W - 2, H - 6, shade(door, 0.8f));
            for (int i = 0; i < 3; ++i) {
                R(3 + i * 20, 6, 18, H - 10, door);
                F(3 + i * 20 + (i % 2 ? 2 : 14), 9, 2, 5, opaque(0x5E5E5E));
            }
            F(1, H - 2, W - 2, 2, opaque(0x2E2E2E));  // toe kick
            break;
        }
        case Piece::Fridge: {
            const uint32_t enamel = opaque(0xE8E2D0);
            R(0, 1, W, H - 3, enamel);
            F(1, 0, W - 2, 1, enamel);
            F(1, H * 2 / 5, W - 2, 1, shade(enamel, 0.6f));
            F(W - 5, 6, 2, 10, opaque(0x9A9A9A));
            F(W - 5, H * 2 / 5 + 4, 2, 14, opaque(0x9A9A9A));
            F(4, 6, 5, 6, opaque(0xF2C230));   // magnets and a note
            F(5, 14, 3, 3, opaque(0xD9302E));
            F(2, H - 2, W - 4, 2, opaque(0x333333));
            break;
        }
        case Piece::Bookshelf: {
            R(0, 0, W, H, walnut);
            Rng rng(uint64_t(W) * 31u + 7u);
            const uint32_t books[6] = {opaque(0xD9302E), opaque(0x3E6FA8), opaque(0xF2C230), opaque(0x4E8A3A),
                                       opaque(0xE8E0CC), opaque(0x7A3E8A)};
            for (int shelf = 0; shelf < 4; ++shelf) {
                const int sy = 3 + shelf * 12;
                F(2, sy, W - 4, 10, shade(walnut, 0.55f));
                int bx = 2;
                while (bx < W - 3) {
                    const int bw = 2 + int(rng.below(2));
                    const int bh = 6 + int(rng.below(4));
                    if (rng.uniform() < 0.12f) {
                        bx += bw;
                        continue;
                    }
                    const uint32_t c = books[rng.below(6)];
                    F(bx, sy + 10 - bh, std::min(bw, W - 2 - bx), bh, c);
                    F(bx, sy + 10 - bh, 1, bh, shade(c, 1.2f));
                    bx += bw;
                }
                F(1, sy + 10, W - 2, 2, shade(walnut, 1.15f));
            }
            break;
        }
        case Piece::Bed: {
            R(0, 0, 7, H, walnut);  // headboard
            R(1, 2, 5, 8, shade(walnut, 1.15f));
            R(7, seatY, W - 7, 6, opaque(0xF4F1EA));  // mattress
            R(9, seatY - 3, 14, 5, opaque(0xFFFFFF));  // pillow
            R(24, seatY - 1, W - 24, 8, opaque(0x4A6FB0));  // blanket
            for (int x = 28; x < W - 2; x += 8) F(x, seatY + 1, 1, 6, opaque(0x3B5A92));
            R(7, seatY + 7, W - 7, H - seatY - 10, walnut);
            F(W - 4, H - 3, 3, 3, walnut);
            break;
        }
        case Piece::Nightstand: {
            R(0, 0, W, H - 2, pine);
            R(2, 3, W - 4, 5, shade(pine, 1.1f));
            F(W / 2 - 1, 5, 2, 1, opaque(0x5E3A22));
            R(2, 9, W - 4, 5, shade(pine, 1.1f));
            F(W / 2 - 1, 11, 2, 1, opaque(0x5E3A22));
            F(1, H - 2, 2, 2, pine);
            F(W - 3, H - 2, 2, 2, pine);
            break;
        }
        case Piece::Desk: {
            R(0, 0, W, 4, oak);
            R(W - 18, 4, 16, H - 4, shade(oak, 0.92f));  // drawer unit
            for (int i = 0; i < 3; ++i) {
                R(W - 16, 6 + i * 5, 12, 4, shade(oak, 1.08f));
                F(W - 11, 7 + i * 5, 3, 1, opaque(0x3A2A1A));
            }
            R(2, 4, 3, H - 4, shade(oak, 0.92f));
            F(5, 4, W - 23, 2, shade(oak, 0.7f));
            break;
        }
        case Piece::Monitor: {
            R(0, 0, W, H - 3, opaque(0xD8D0B8));
            R(2, 2, W - 4, H - 8, opaque(0x183018));
            for (int y = 3; y < H - 7; y += 2) F(3, y, 3 + (y * 7) % 9, 1, opaque(0x4CE060));
            F(W / 2 - 3, H - 3, 6, 2, opaque(0xC8C0A8));
            F(W / 2 - 5, H - 1, 10, 1, opaque(0xB0A890));
            break;
        }
        case Piece::OfficeChair: {
            const uint32_t seat = opaque(0x2E2E36);
            R(W - 3, 0, 3, seatY + 2, seat);  // backrest on the right
            R(W - 4, 2, 2, 10, shade(seat, 1.3f));
            R(0, seatY, W - 2, 3, seat);
            F(W / 2 - 1, seatY + 3, 2, H - seatY - 6, opaque(0x8A8A8A));  // gas lift
            F(1, H - 3, W - 2, 1, opaque(0x5A5A5A));
            F(1, H - 2, 2, 2, opaque(0x1A1A1A));
            F(W - 3, H - 2, 2, 2, opaque(0x1A1A1A));
            F(W / 2 - 1, H - 2, 2, 2, opaque(0x1A1A1A));
            break;
        }
        case Piece::Wardrobe: {
            R(0, 2, W, H - 4, walnut);
            R(0, 0, W, 3, shade(walnut, 1.2f));  // crown
            R(2, 5, W / 2 - 3, H - 11, shade(walnut, 1.12f));
            R(W / 2 + 1, 5, W / 2 - 3, H - 11, shade(walnut, 1.12f));
            F(W / 2 - 3, H / 2, 1, 4, opaque(0xD8C080));
            F(W / 2 + 2, H / 2, 1, 4, opaque(0xD8C080));
            F(2, H - 2, 3, 2, walnut);
            F(W - 5, H - 2, 3, 2, walnut);
            break;
        }
        case Piece::Dresser: {
            const uint32_t white = opaque(0xE6DFD0);
            R(0, 0, W, H - 2, white);
            for (int i = 0; i < 3; ++i) {
                R(2, 3 + i * 7, W - 4, 6, shade(white, 1.04f));
                F(W / 2 - 6, 5 + i * 7, 2, 2, opaque(0xB9853E));
                F(W / 2 + 4, 5 + i * 7, 2, 2, opaque(0xB9853E));
            }
            F(2, H - 2, 3, 2, opaque(0x8A7A60));
            F(W - 5, H - 2, 3, 2, opaque(0x8A7A60));
            break;
        }
        case Piece::FilingCabinet: {
            const uint32_t metal = opaque(0x8C949C);
            R(0, 0, W, H, metal);
            for (int i = 0; i < 3; ++i) {
                R(2, 2 + i * 9, W - 4, 8, shade(metal, 1.08f));
                F(W / 2 - 3, 5 + i * 9, 6, 1, opaque(0x3A3A3A));
                F(W / 2 - 2, 3 + i * 9, 4, 1, opaque(0xF4EEDC));
            }
            break;
        }
        case Piece::Count:
            break;
    }
    outlineSprite(img);
    return img;
}

Image debrisSprite(Piece p) {
    const PieceSpec& sp = pieceSpec(p);
    const int w = std::max(10, std::min(sp.w, 40));
    Image img(w + 2, 8);
    const bool screen = p == Piece::Tv || p == Piece::Monitor;
    const uint32_t c = screen ? opaque(0x3A3634) : (p == Piece::Stool || p == Piece::Nightstand ? opaque(0xC8955A)
                                                     : p == Piece::CoffeeTable ? opaque(0x5E3A22) : opaque(0xA06C3C));
    Rng rng(uint64_t(p) * 131u + 5u);
    for (int i = 0; i < 6; ++i) {
        const int len = w / 3 + int(rng.below(uint32_t(w / 3 + 1)));
        const int x0 = 1 + int(rng.below(uint32_t(std::max(1, w - len))));
        const int y0 = 3 + int(rng.below(4));
        const int y1 = 3 + int(rng.below(4));
        img.line(x0, y0, x0 + len, y1, i % 2 ? c : shade(c, 1.2f));
        img.line(x0, y0 + 1, x0 + len, y1 + 1, shade(c, 0.8f));
    }
    if (screen) {
        img.set(w / 2, 4, opaque(0xB8F0E8));
        img.set(w / 2 + 3, 5, opaque(0xB8F0E8));
    }
    outlineSprite(img);
    return img;
}

// ---------------------------------------------------------------- logo and icon

int logoHeight(int cell) { return font::kGlyphH * cell + 3; }

void drawLogo(Image& img, int cx, int y, int cell) {
    const std::string title = "KARASHEKA";
    const int adv = font::kAdvance * cell + cell;
    const int w = int(title.size()) * adv - cell;
    const int x0 = cx - w / 2;
    const int h = font::kGlyphH * cell;
    // Shadow, outline, then a fiery gradient fill.
    for (int pass = 0; pass < 3; ++pass) {
        for (size_t i = 0; i < title.size(); ++i) {
            const char* const* rows = font::glyph(title[i]);
            if (!rows) continue;
            const int gx = x0 + int(i) * adv;
            for (int ry = 0; ry < font::kGlyphH; ++ry)
                for (int rx = 0; rx < font::kGlyphW; ++rx) {
                    if (rows[ry][rx] != '#') continue;
                    const int px = gx + rx * cell, py = y + ry * cell;
                    if (pass == 0) {
                        img.fill(px + 2, py + 3, cell, cell, argb(0, 0, 0, 150));
                    } else if (pass == 1) {
                        img.fill(px - 1, py - 1, cell + 2, cell + 2, kInk);
                    } else {
                        for (int k = 0; k < cell; ++k) {
                            const float t = float(py + k - y) / float(h);
                            uint32_t c = t < 0.45f ? mixc(opaque(0xFFF1A8), opaque(0xFFB238), t / 0.45f)
                                                   : mixc(opaque(0xFFB238), opaque(0xD9302E), (t - 0.45f) / 0.55f);
                            if (k == 0 && ry > 0 && rows[ry - 1][rx] != '#') c = shade(c, 1.15f);
                            img.fill(px, py + k, cell, 1, c);
                        }
                    }
                }
        }
    }
}

void icon(const Pen& p, bool background) {
    if (background) {
        p.roundRect(0.02f, 0.02f, 0.98f, 0.98f, 0.18f, VGrad{0.f, 1.f, rgb(0xF6EAD0), rgb(0xE8D2A8)});
    }
    p.circle(0.5f, 0.47f, 0.34f, solid(rgb(0xD9302E)));
    // A flying kick silhouette, from the game's own skeleton.
    Pose kick = pose(PoseId::FlyStrike);
    const Skeleton s = solve(kick, 0.f, 0.f, 1);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    const Vec pts[] = {s.head, s.hip, s.shoulder, s.hand[0], s.hand[1], s.toe[0], s.toe[1], s.knee[0], s.knee[1]};
    for (const Vec& v : pts) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    const float span = std::max(maxX - minX, maxY - minY) + 12.f;
    const float scale = 0.74f / span;
    const float ox = 0.5f - (minX + maxX) * 0.5f * scale, oy = 0.5f - (minY + maxY) * 0.5f * scale;
    auto map = [&](Vec v) { return Vec{ox + v.x * scale, oy + v.y * scale}; };
    const RGBA ink = rgb(0x1E1619);
    auto limb = [&](Vec a, Vec b, float r) {
        const Vec A = map(a), B = map(b);
        const float R = r * scale;
        p.fill(std::min(A.x, B.x) - R, std::min(A.y, B.y) - R, std::max(A.x, B.x) + R, std::max(A.y, B.y) + R,
               [=](float x, float y) { return segmentDistance(Vec{x, y}, A, B) <= R; }, solid(ink));
    };
    for (int i = 0; i < 2; ++i) {
        limb(s.shoulder, s.elbow[i], 3.f);
        limb(s.elbow[i], s.hand[i], 2.6f);
        limb(s.hip, s.knee[i], 4.f);
        limb(s.knee[i], s.ankle[i], 3.4f);
        limb(s.ankle[i], s.toe[i], 2.f);
    }
    limb(s.hip, s.shoulder, 5.8f);
    limb(s.shoulder, s.neck, 2.6f);
    limb(s.head, s.head, kHeadR + 0.6f);
    // Headband tails.
    const Vec knot = s.head + Vec{-4.f, -1.f};
    limb(knot, knot + Vec{-9.f, 1.f}, 1.2f);
    limb(knot, knot + Vec{-7.f, 5.f}, 1.2f);
}

}  // namespace kara::art
