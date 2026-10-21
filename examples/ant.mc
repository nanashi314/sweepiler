// Langton's ant on a 12 x 12 torus: on a white cell turn right, on a black
// cell turn left, flip the cell and step forward. Run pauses and resumes.
#display 12 12
input press run;

const int N = 12;

bool grid[144];
uint4 ax = 6;
uint4 ay = 6;
uint2 dir = 0;          // 0 up, 1 right, 2 down, 3 left
bool paused = false;
bool blink = false;

void tick() {
    blink = !blink;
    if (run) paused = !paused;
    if (paused) return;
    uint8 here = (uint8)ay * N + ax;
    bool black = grid[here];
    if (black) dir--; else dir++;
    grid[here] = !black;
    if (dir == 0) ay = ay == 0 ? N - 1 : ay - 1;
    if (dir == 1) ax = ax == N - 1 ? 0 : ax + 1;
    if (dir == 2) ay = ay == N - 1 ? 0 : ay + 1;
    if (dir == 3) ax = ax == 0 ? N - 1 : ax - 1;
}

void draw() {
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            pixel(x, y, grid[y * N + x] ^ (blink && ax == x && ay == y));
}
