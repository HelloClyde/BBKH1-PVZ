#pragma once
#include <cstdint>
namespace H1Combat {
constexpr uint64_t FlatKey=0x4658000000000000ull,HitKey=0x5053000000000000ull,HatKey=0x4841000000000000ull;
constexpr unsigned Variants=4,HitLastAge=96;
struct Scale {float x,y;};
constexpr Scale Scales[]={{1,1},{1.3f,1.3f},{1.4f,1.4f},{1.7f,1.7f},{2,2},{3,1},{1.6f,1.6f},{.82f,.82f}};
}
