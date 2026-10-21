// Tetris in a 6 x 10 well. Left / right move, rotate turns, down drops faster.
#display 6 10
input hold left;
input hold right;
input press rotate;
input hold down;

const int W = 6;
const int H = 10;
const int WALLS = 0x3C0F;   // a row padded with 4 wall cells on each side

uint6 rows[10];     // the well, row 0 at the top; bit x is column x
uint3 kind = 2;     // 0..6: I O T S Z J L
uint2 turn = 0;
int5 px = 1;        // the piece's 4 x 4 box: left column ...
int5 py = 0;        // ... and top row
uint2 fall = 0;     // the piece falls one row every fourth tick
uint8 seed = 0x5D;

// The piece as a 4 x 4 mask, bit dy * 4 + dx.
uint16 shape(uint3 k, uint2 r) {
    if (k == 0) return r == 0 ? 0x00F0 : r == 1 ? 0x4444 : r == 2 ? 0x0F00 : 0x2222;
    if (k == 1) return 0x0066;
    if (k == 2) return r == 0 ? 0x0072 : r == 1 ? 0x0262 : r == 2 ? 0x0270 : 0x0232;
    if (k == 3) return r == 0 ? 0x0036 : r == 1 ? 0x0462 : r == 2 ? 0x0360 : 0x0231;
    if (k == 4) return r == 0 ? 0x0063 : r == 1 ? 0x0264 : r == 2 ? 0x0630 : 0x0132;
    if (k == 5) return r == 0 ? 0x0071 : r == 1 ? 0x0226 : r == 2 ? 0x0470 : 0x0322;
    return r == 0 ? 0x0074 : r == 1 ? 0x0622 : r == 2 ? 0x0170 : 0x0223;
}

// Row dy of the mask at column x of a padded row. OR-ing one term per
// possible column is cheaper in gates than shifting by a variable amount.
uint14 placed(uint16 m, int dy, int5 x) {
    uint14 bits = (uint14)(m >> (dy * 4) & 15);
    uint14 out = 0;
    for (int k = -3; k <= 6; k++)
        out |= x == k ? bits << (k + 4) : 0;
    return out;
}

// Whether the piece fits with its box at (x, y). Well rows are picked by
// comparing y with constants, which shares one decoder for all of them.
bool fits(uint16 m, int5 x, int5 y) {
    for (int dy = 0; dy < 4; dy++) {
        // Walls above the well, the floor below it, and the row in between.
        uint14 well = y >= H - dy ? 0x3FFF : WALLS;
        for (int r = 0; r < H; r++)
            well |= y == r - dy ? (uint14)rows[r] << 4 : 0;
        if ((placed(m, dy, x) & well) != 0) return false;
    }
    return true;
}

void tick() {
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;

    // One turn or sideways step per tick, kept only if the piece still fits.
    uint16 m = shape(kind, turn);
    uint2 nt = turn;
    int5 nx = px;
    if (rotate) nt = turn + 1;
    else if (left) nx = px - 1;
    else if (right) nx = px + 1;
    uint16 moved = rotate ? shape(kind, nt) : m;
    if (fits(moved, nx, py)) { turn = nt; px = nx; m = moved; }

    // Gravity: fall, or lock the piece and bring the next one.
    fall++;
    if (down || fall == 0) {
        if (fits(m, px, py + 1)) py++;
        else {
            if (py <= 0) {
                // Stuck at the top: the well is full, start over.
                for (int r = 0; r < H; r++) rows[r] = 0;
            } else {
                uint6 add[4];
                for (int dy = 0; dy < 4; dy++) add[dy] = (uint6)(placed(m, dy, px) >> 4);
                for (int r = 0; r < H; r++) {
                    uint6 lock = 0;
                    for (int dy = 0; dy < 4; dy++) lock |= py == r - dy ? add[dy] : 0;
                    rows[r] |= lock;
                }
            }
            uint3 k = (uint3)seed;
            if (k == 7) k = (uint3)(seed >> 4) & 3;
            kind = k; turn = 0; px = 1; py = 0;
        }
    }

    // Clear the lowest full row; everything above it moves down one.
    bool found = false;
    for (int r = H - 1; r >= 0; r--) {
        if (rows[r] == 63) found = true;
        if (found) rows[r] = r > 0 ? rows[r - 1] : 0;
    }
}

void draw() {
    uint16 m = shape(kind, turn);
    uint14 piece[4];
    for (int dy = 0; dy < 4; dy++) piece[dy] = placed(m, dy, px);
    for (int y = 0; y < H; y++) {
        uint14 here = 0;
        for (int dy = 0; dy < 4; dy++) here |= py == y - dy ? piece[dy] : 0;
        for (int x = 0; x < W; x++)
            pixel(x, y, (rows[y] >> x & 1) | (here >> (x + 4) & 1));
    }
}
