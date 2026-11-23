#include "core/room.h"

#include <algorithm>

namespace kara {
namespace {

// clang-format off
const PieceSpec kSpecs[kPieceCount] = {
    //  name              w   h  top seat0 seat1  hp
    {"CHAIR",            16, 30, 13,   4,  16,    2},
    {"STOOL",            12, 18, 18,   0,  12,    1},
    {"TABLE",            48, 21, 21,   0,  48,    4},
    {"COFFEE TABLE",     36, 11, 11,   0,  36,    3},
    {"SOFA",             66, 26, 13,   6,  60,   -1},
    {"ARMCHAIR",         30, 26, 13,   5,  25,   -1},
    {"TV CABINET",       42, 20, 20,   0,  42,   -1},
    {"TV",               30, 19, 19,   1,  29,    1},
    {"COUNTER",          62, 28, 28,   0,  62,   -1},
    {"FRIDGE",           26, 52, 52,   0,  26,   -1},
    {"BOOKSHELF",        30, 52, 52,   0,  30,   -1},
    {"BED",              74, 26, 14,   7,  74,   -1},
    {"NIGHTSTAND",       18, 17, 17,   0,  18,    2},
    {"DESK",             50, 22, 22,   0,  50,    4},
    {"MONITOR",          18, 14, 14,   0,  18,    1},
    {"OFFICE CHAIR",     16, 30, 14,   0,  13,    2},
    {"WARDROBE",         36, 54, 54,   0,  36,   -1},
    {"DRESSER",          44, 26, 26,   0,  44,   -1},
    {"FILING CABINET",   20, 30, 30,   0,  20,   -1},
};
// clang-format on

struct Place {
    Piece kind;
    float x;
    bool flip;
    float base;
};

struct RoomDef {
    const char* name;
    std::vector<Place> pieces;
};

const RoomDef& roomDef(int room) {
    static const RoomDef kRooms[kRoomCount] = {
        {"LIVING ROOM",
         {{Piece::Bookshelf, 14, false, 0},
          {Piece::Sofa, 48, false, 0},
          {Piece::CoffeeTable, 134, false, 0},
          {Piece::Armchair, 214, true, 0},
          {Piece::TvCabinet, 264, false, 0},
          {Piece::Tv, 270, false, 20}}},
        {"KITCHEN",
         {{Piece::Fridge, 12, false, 0},
          {Piece::Counter, 38, false, 0},
          {Piece::Chair, 126, false, 0},
          {Piece::Table, 144, false, 0},
          {Piece::Chair, 194, true, 0},
          {Piece::Stool, 262, false, 0}}},
        {"BEDROOM",
         {{Piece::Wardrobe, 12, false, 0},
          {Piece::Bed, 52, false, 0},
          {Piece::Nightstand, 128, false, 0},
          {Piece::Dresser, 236, false, 0},
          {Piece::Chair, 288, true, 0}}},
        {"STUDY",
         {{Piece::Bookshelf, 12, false, 0},
          {Piece::Desk, 48, false, 0},
          {Piece::Monitor, 70, false, 22},
          {Piece::OfficeChair, 100, true, 0},
          {Piece::FilingCabinet, 238, false, 0},
          {Piece::Bookshelf, 278, false, 0}}},
    };
    return kRooms[((room % kRoomCount) + kRoomCount) % kRoomCount];
}

}  // namespace

const PieceSpec& pieceSpec(Piece p) { return kSpecs[std::clamp(int(p), 0, kPieceCount - 1)]; }

float Furniture::x0() const {
    const PieceSpec& s = spec();
    return flip ? x + float(s.w - s.seat1) : x + float(s.seat0);
}

float Furniture::x1() const {
    const PieceSpec& s = spec();
    return flip ? x + float(s.w - s.seat0) : x + float(s.seat1);
}

float Furniture::topY() const { return kFloorY - base - float(spec().top); }

const char* roomName(int room) { return roomDef(room).name; }

std::vector<Furniture> furnish(int room) {
    std::vector<Furniture> out;
    for (const Place& p : roomDef(room).pieces) {
        Furniture f;
        f.kind = p.kind;
        f.x = p.x;
        f.flip = p.flip;
        f.base = p.base;
        f.hp = pieceSpec(p.kind).hp;
        out.push_back(f);
    }
    return out;
}

}  // namespace kara
