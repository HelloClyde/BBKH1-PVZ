/* One build-time contract for raster stride, damage tiles and input mapping. */
#pragma once
namespace Sexy {
#ifdef PVZH1_WIDESCREEN
inline constexpr unsigned H1ScreenWidth=480,H1ScreenHeight=272;
inline constexpr unsigned H1LogicalWidth=1060,H1LogicalHeight=600;
inline constexpr unsigned H1TileWidth=16,H1TileHeight=8;
#else
inline constexpr unsigned H1ScreenWidth=360,H1ScreenHeight=270;
inline constexpr unsigned H1LogicalWidth=800,H1LogicalHeight=600;
inline constexpr unsigned H1TileWidth=12,H1TileHeight=10;
#endif
inline constexpr unsigned H1BoardOffset=(H1LogicalWidth-800)/2;
inline int H1BoardCameraX(int original){
    int value=original+(int)H1BoardOffset;
#ifdef PVZH1_WIDESCREEN
    // Original campaign backdrops span 1400 logical pixels. The wider view
    // needs a smaller camera pan so neither edge leaves the painted scene.
    constexpr int minimum=(int)H1LogicalWidth+220-1400,maximum=220;
    if(value<minimum)value=minimum;if(value>maximum)value=maximum;
#endif
    return value;
}
static_assert(H1ScreenWidth%H1TileWidth==0&&H1ScreenWidth/H1TileWidth<32);
static_assert(H1ScreenHeight%H1TileHeight==0);
}
