// Dice: press roll for a number from 1 to 6, shown as pips.
#display 7 7
input press roll;

uint3 face = 1;
uint8 seed = 0x2D;

void tick() {
    // A shift register with feedback (an LFSR) changes every tick, so the
    // moment you press decides the number.
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;
    if (roll) {
        uint3 r = (uint3)seed;       // 0..7
        if (r == 0) r = 1;           // fold 0 and 7 into the dice (1 and 6
        if (r == 7) r = 6;           // come up a little more often)
        face = r;
    }
}

void draw() {
    pixel(3, 3, (face & 1) != 0);                     // the middle pip: odd numbers
    pixel(1, 1, face >= 2); pixel(5, 5, face >= 2);   // two corners from 2 up
    pixel(5, 1, face >= 4); pixel(1, 5, face >= 4);   // the other two from 4 up
    pixel(1, 3, face == 6); pixel(5, 3, face == 6);   // the middle row for 6
}
