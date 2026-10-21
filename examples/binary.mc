// Binary: an 8-bit number counting up, shown as 8 lights.
// The rightmost light is bit 0. Press "pause" to stop and start it.
#display 8 3
input press pause;   // "press": true for one tick each time the key goes down

uint8 n = 0;         // 0..255, then it wraps back to 0
bool paused = false;

void tick() {
    if (pause) paused = !paused;
    if (!paused) n++;
}

void draw() {
    // A for loop with fixed bounds; i is a constant in each copy of the body.
    for (int i = 0; i < 8; i++)
        pixel(7 - i, 1, n >> i & 1);
}
