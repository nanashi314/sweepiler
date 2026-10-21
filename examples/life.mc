// Conway's Game of Life on an 8 x 8 torus with a cursor to draw cells.
// Arrow keys move the cursor, A toggles the cell under it, B runs or pauses.
#display 8 8
input press left;
input press right;
input press up;
input press down;
input press toggle;
input press run;

bool cell[64] = {0, 1, 0, 0, 0, 0, 0, 0,
                 0, 0, 1, 0, 0, 0, 0, 0,
                 1, 1, 1};          // a glider
uint3 cx = 4;
uint3 cy = 4;
bool running = true;
uint2 clock = 0;
bool blink = false;

void tick() {
    blink = !blink;
    if (left) cx--;
    if (right) cx++;
    if (up) cy--;
    if (down) cy++;
    if (run) running = !running;
    if (toggle) cell[(uint6)cy * 8 + cx] = !cell[(uint6)cy * 8 + cx];
    clock++;
    if (!running || clock != 0) return;

    // Column sums of three cells are shared by the three cells beside them;
    // the 3 x 3 total t (self included) decides: alive next iff t == 3,
    // or t == 4 and alive now.
    uint2 col[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            col[y * 8 + x] = (uint2)cell[((y + 7) & 7) * 8 + x] + cell[y * 8 + x] + cell[((y + 1) & 7) * 8 + x];
        }
    bool next[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint4 t = (uint4)col[y * 8 + ((x + 7) & 7)] + col[y * 8 + x] + col[y * 8 + ((x + 1) & 7)];
            next[y * 8 + x] = t == 3 || (t == 4 && cell[y * 8 + x]);
        }
    for (int i = 0; i < 64; i++) cell[i] = next[i];
}

void draw() {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            bool here = cx == x && cy == y;
            pixel(x, y, here ? cell[y * 8 + x] ^ blink : cell[y * 8 + x]);
        }
}
