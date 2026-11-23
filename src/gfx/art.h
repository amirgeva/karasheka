#pragma once

#include <cstdint>

#include "core/pose.h"
#include "core/room.h"
#include "gfx/canvas.h"
#include "gfx/image.h"

// Procedural pixel art. Nothing here touches SDL: everything is drawn into
// an Image (or, for the app icon, an anti-aliased Canvas).
namespace kara::art {

constexpr int kLooks = 2;
uint32_t playerColor(int fighter);  // HUD colour: the headband

// A karateka drawn from its skeleton: gi, belt, headband, outlined pixels.
// `flash` draws it white (just hit); `time` waves the headband tails.
void drawFighter(Image& img, const Skeleton& s, int look, bool flash, float time);

// The room behind the action: wall, window, pictures, floor. Opaque 320 x 180.
void paintRoom(Image& img, int room);

// Furniture, unflipped and with a one-pixel outline all round: the sprite is
// (w + 2) x (h + 2) and its piece's bottom-left corner is at (1, h + 1).
Image pieceSprite(Piece p);
Image debrisSprite(Piece p);  // what is left on the floor once it breaks

// "KARASHEKA" in chunky letters, centred on cx, `cell` pixels per font pixel.
void drawLogo(Image& img, int cx, int y, int cell);
int logoHeight(int cell);

// The app icon: a flying kick against a red sun. Unit square.
void icon(const Pen& p, bool background);

}  // namespace kara::art
