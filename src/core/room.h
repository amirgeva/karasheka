#pragma once

// The apartment: rooms with a flat floor and furniture the fighters can jump
// onto. Every piece is a one-way platform: you walk in front of it, land on
// its top when you come down from above, and fall off when you walk past an
// edge. Light pieces break after a few heavy landings.

#include <vector>

namespace kara {

constexpr int kWorldW = 320;  // the screen and the room, in world pixels
constexpr int kWorldH = 180;
constexpr float kFloorY = 166.f;  // the floor line the fighters stand on
constexpr float kWallL = 12.f;    // fighters' centres stay between the walls
constexpr float kWallR = 308.f;
constexpr int kRoomCount = 4;

enum class Piece : int {
    Chair, Stool, Table, CoffeeTable, Sofa, Armchair, TvCabinet, Tv, Counter, Fridge, Bookshelf,
    Bed, Nightstand, Desk, Monitor, OfficeChair, Wardrobe, Dresser, FilingCabinet,
    Count
};
constexpr int kPieceCount = int(Piece::Count);

struct PieceSpec {
    const char* name;
    int w, h;          // sprite size in world pixels
    int top;           // height of the surface you can stand on, above the piece's base
    int seat0, seat1;  // the standable span, measured from the unflipped left edge
    int hp;            // heavy landings it survives; -1 = sturdy
};
const PieceSpec& pieceSpec(Piece p);

struct Furniture {
    Piece kind = Piece::Chair;
    float x = 0.f;       // left edge
    float base = 0.f;    // height its feet stand at above the floor (a TV on a cabinet)
    bool flip = false;   // mirrored left to right
    int hp = -1;         // remaining; -1 = sturdy
    bool broken = false;
    float wobble = 0.f;  // 1 right after a landing or a knock, fades to 0 (cosmetic)

    const PieceSpec& spec() const { return pieceSpec(kind); }
    float x0() const;    // standable span, world x
    float x1() const;
    float topY() const;  // world y of the standable surface
    float baseY() const { return kFloorY - base; }
};

const char* roomName(int room);
std::vector<Furniture> furnish(int room);

}  // namespace kara
