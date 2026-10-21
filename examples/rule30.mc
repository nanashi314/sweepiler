// Elementary cellular automata on a ring of 16 cells: each tick adds a
// generation at the bottom and the picture scrolls up. Rule switches between
// rules 30, 90 and 110; reset starts again from one cell.
#display 16 12
input press rule;
input press reset;

uint16 rows[12];
uint2 which = 0;       // 0: rule 30, 1: rule 90, 2: rule 110
bool started = false;

uint16 next(uint16 c, uint2 r) {
    uint16 l = c << 1 | c >> 15;    // each cell's left neighbour
    uint16 n = c >> 1 | c << 15;    // and right neighbour
    if (r == 0) return l ^ (c | n);
    if (r == 1) return l ^ n;
    return (c ^ n) | (c & ~l);
}

void tick() {
    if (rule) which = which == 2 ? 0 : which + 1;
    if (reset || rule || !started) {
        for (int i = 0; i < 12; i++) rows[i] = 0;
        rows[11] = 0x0100;
        started = true;
        return;
    }
    uint16 last = rows[11];
    for (int i = 0; i < 11; i++) rows[i] = rows[i + 1];
    rows[11] = next(last, which);
}

void draw() {
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 16; x++)
            pixel(x, y, rows[y] >> x & 1);
}
