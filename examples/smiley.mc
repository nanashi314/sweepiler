// Smiley: a picture drawn from a function that returns one row at a time.
// Bit x of a row is the pixel in column x. Press "wink" to wink.
#display 8 8
input press wink;

bool winking = false;

// Constants only: this costs almost no gates, and no state.
uint8 row(int y) {
    if (y == 0) return 0x3C;   // ..####..
    if (y == 1) return 0x42;   // .#....#.
    if (y == 2) return 0xA5;   // #.#..#.#  the eyes
    if (y == 3) return 0x81;   // #......#
    if (y == 4) return 0xA5;   // #.#..#.#  the corners of the mouth
    if (y == 5) return 0x99;   // #..##..#
    if (y == 6) return 0x42;   // .#....#.
    return 0x3C;               // ..####..
}

void tick() {
    if (wink) winking = !winking;
}

void draw() {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            bool on = (row(y) >> x & 1) != 0;
            if (y == 2 && x == 5 && winking) on = false;   // close one eye
            pixel(x, y, on);
        }
}
