// Flappy: press flap to rise, fly through the gaps in the pipes.
#display 8 8
input press flap;

const int GAP = 3;     // gap height

uint3 y = 3;           // the bird, in column 1
bool lift = false;     // a flap waits for the next move
uint3 pipe = 7;        // the pipe's column
uint3 gap = 2;         // top row of the gap
bool slow = false;
uint3 crash = 0;       // flashes after a crash
uint8 seed = 0xA7;

void tick() {
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;
    if (flap) lift = true;
    if (crash > 0) {
        crash--;
        if (crash == 0) { y = 3; pipe = 7; }
        return;
    }
    slow = !slow;
    if (!slow) return;

    // The bird rises two rows after a flap and falls one otherwise.
    if (lift) y = y > 2 ? y - 2 : 0;
    else if (y < 7) y++;
    lift = false;

    // The pipe moves left; a new one comes in with a random gap.
    if (pipe == 0) {
        pipe = 7;
        gap = (uint3)(seed & 3) + 1;
    } else pipe--;

    if (pipe == 1 && (y < gap || y >= gap + GAP)) crash = 6;
}

void draw() {
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            bool wall = pipe == i && (j < gap || j >= gap + GAP);
            bool bird = i == 1 && y == j;
            pixel(i, j, crash > 0 ? (crash & 1) != 0 : wall || bird);
        }
}
