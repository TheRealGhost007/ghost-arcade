#include "palette.h"

const Color kWallTop = {30, 22, 64, 255};
const Color kWallBottom = {21, 16, 43, 255};
const Color kFloor = {15, 11, 33, 255};
const Color kBaseboard = {62, 53, 110, 255};
const Color kCabinet = {43, 36, 80, 255};
const Color kCabinetDark = {28, 23, 56, 255};
const Color kCabinetEdge = {70, 60, 124, 255};
const Color kScreenOff = {10, 8, 22, 255};
const Color kScreenOn = {18, 22, 46, 255};
const Color kInk = {21, 16, 43, 255};          /* text printed on a lit marquee */
const Color kWarmWhite = {255, 246, 224, 255}; /* backlit plexi */
const Color kText = {242, 236, 255, 255};
const Color kTextDim = {154, 145, 196, 255};
const Color kFaint = {96, 88, 150, 255};       /* the house "faint": disabled controls */
const Color kPanel = {34, 27, 70, 255};        /* the house panel: under the focused control */
const Color kCoinRed = {255, 59, 48, 255};
const Color kPink = {255, 79, 163, 255};
const Color kTeal = {33, 212, 200, 255};
const Color kYellow = {255, 210, 63, 255};
const Color kCard = {12, 9, 28, 240};

Color Pal_Mix(Color a, Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t),
        255,
    };
}
