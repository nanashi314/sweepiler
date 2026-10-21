// Snake on an 8 x 8 torus. The body is a shift register of positions plus an
// occupancy grid, so moving touches only the new head and the old tail.
#display 8 8
input hold left;
input hold right;
input hold up;
input hold down;

const int MAXLEN = 12;

uint3 sx[12] = {3, 2, 1};   // segment 0 is the head
uint3 sy[12] = {3, 3, 3};
bool occ[64] = {0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 1, 1, 1};
uint4 len = 3;
uint2 dir = 0;              // 0 right, 1 down, 2 left, 3 up
uint2 moved = 0;            // the direction of the last step: no turning back on it
uint2 clock = 0;            // the snake moves every fourth tick
uint6 food = 45;
bool hungry = false;        // looking for a free cell for the next food
uint8 seed = 0x5a;
bool dead = false;
uint4 rest = 0;             // ticks the dead snake blinks before a key restarts

uint6 cell(uint3 x, uint3 y) { return (uint6)y * 8 + x; }

void reset() {
    for (int i = 0; i < 64; i++) occ[i] = false;
    for (int k = 0; k < MAXLEN; k++) { sx[k] = 0; sy[k] = 0; }
    sx[0] = 3; sx[1] = 2; sx[2] = 1;
    sy[0] = 3; sy[1] = 3; sy[2] = 3;
    occ[27] = true; occ[26] = true; occ[25] = true;
    len = 3; dir = 0; moved = 0; food = 45; hungry = false; dead = false;
}

void tick() {
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;
    clock++;
    if (dead) {
        if (rest > 0) rest--;
        else if (left || right || up || down) reset();
        return;
    }
    if (right && moved != 2) dir = 0;
    else if (down && moved != 3) dir = 1;
    else if (left && moved != 0) dir = 2;
    else if (up && moved != 1) dir = 3;

    if (hungry) {
        uint6 spot = (uint6)seed;
        if (!occ[spot]) { food = spot; hungry = false; }
    }

    if (clock != 0) return;

    uint3 nx = sx[0];
    uint3 ny = sy[0];
    if (dir == 0) nx++;
    if (dir == 1) ny++;
    if (dir == 2) nx--;
    if (dir == 3) ny--;
    uint6 head = cell(nx, ny);
    bool eat = !hungry && head == food;

    // The tail moves on unless the snake grows (at full length it moves on anyway),
    // so the head may take the cell the tail leaves.
    uint6 tail = cell(sx[len - 1], sy[len - 1]);
    bool frees = !eat || len == MAXLEN;
    if (occ[head] && !(frees && head == tail)) { dead = true; rest = 12; return; }
    if (frees) occ[tail] = false;
    occ[head] = true;
    for (int k = MAXLEN - 1; k > 0; k--) { sx[k] = sx[k - 1]; sy[k] = sy[k - 1]; }
    sx[0] = nx;
    sy[0] = ny;
    moved = dir;
    if (eat) {
        if (len < MAXLEN) len++;
        hungry = true;
    }
}

// The food blinks; so does the whole snake once it is dead.
void draw() {
    bool blink = clock >= 2;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            bool on = (occ[y * 8 + x] && (!dead || blink)) || (!hungry && food == y * 8 + x && blink);
            pixel(x, y, on);
        }
}
