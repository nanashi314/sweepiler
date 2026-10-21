// Mandelbrot set, one iteration per tick, scanning the screen pixel by pixel.
// Fixed point: int8 with 4 fraction bits (1.0 = 16). The set is symmetric
// about the real axis, so only the top half is computed and draw() mirrors it.
#display 16 16
input press zoom;     // switch between the whole set and a closer view

const int MAX = 10;   // iterations before a point counts as inside

bool img[128];        // rows 0..7, the top half
bool line[16];        // the current row, shifted in from the right
uint4 px = 0;
uint3 py = 0;
int8 zr = 0;
int8 zi = 0;
uint4 n = 0;
bool zoomed = false;

// v * v for |v| < 256, through the magnitude so the multiplier stays unsigned.
uint16 square(int9 v) {
    uint9 m = v < 0 ? -v : v;
    return (uint16)m * m;
}

// The same square modulo 2^12: enough where only bits 4..11 are kept.
uint12 square12(int9 v) {
    uint9 m = v < 0 ? -v : v;
    return (uint12)m * m;
}

void tick() {
    // Pixel centres: x from -2.3 (or -1.75 zoomed), y from just above the axis upwards.
    int8 cr = zoomed ? (int8)px * 2 - 28 : (int8)px * 3 - 37;
    int8 ci = zoomed ? -((int8)(7 - py) * 2 + 1) : -((int8)(7 - py) * 3 + 1);
    uint16 a = square(zr);          // zr^2 in 1/256 units
    uint16 b = square(zi);
    bool escaped = a + b > 1024;    // |z|^2 > 4
    if (escaped || n == MAX) {
        for (int k = 0; k < 15; k++) line[k] = line[k + 1];
        line[15] = !escaped;
        if (px == 15)
            for (int r = 0; r < 8; r++)
                if (py == r)
                    for (int x = 0; x < 16; x++) img[r * 16 + x] = line[x];
        zr = 0;
        zi = 0;
        n = 0;
        px++;
        if (px == 0) py++;
    } else {
        // New z in 12-bit arithmetic: the int8 result keeps bits 4..11 only.
        int12 cross = (int12)square12((int9)zr + zi) - (int12)a - (int12)b;   // 2 zr zi
        zr = (int8)(((int12)a - (int12)b) >> 4) + cr;
        zi = (int8)(cross >> 4) + ci;
        n++;
    }
    if (zoom) {
        zoomed = !zoomed;
        px = 0; py = 0; zr = 0; zi = 0; n = 0;
    }
}

void draw() {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            pixel(x, y, img[(y < 8 ? y : 15 - y) * 16 + x]);
}
