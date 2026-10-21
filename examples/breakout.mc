// Breakout: bounce the ball off the paddle and clear the bricks.
#display 8 10
input hold left;
input hold right;

uint8 bricks[3] = {255, 255, 255};   // rows 0..2; bit x is column x
uint3 paddle = 3;    // left cell of the two-cell paddle on row 9
uint3 bx = 4;
uint4 by = 6;
bool right_ = true;  // the ball moves right
bool down_ = false;  // the ball moves down
bool slow = false;   // the ball moves every other tick
uint2 wait = 0;      // a short pause after a miss

void tick() {
    if (left && paddle > 0) paddle--;
    else if (right && paddle < 6) paddle++;
    slow = !slow;
    if (wait > 0) { wait--; return; }
    if (!slow) return;

    // Side and top walls turn the ball around.
    if (bx == 0) right_ = true;
    if (bx == 7) right_ = false;
    if (by == 0) down_ = true;
    uint3 nx = right_ ? bx + 1 : bx - 1;
    uint4 ny = down_ ? by + 1 : by - 1;

    // A brick in the way breaks and the ball bounces: the one above or below
    // turns it vertically, else the one beside it sideways, else the one on
    // the diagonal both ways.
    bool ahead = false;
    bool beside = false;
    bool corner = false;
    for (int r = 0; r < 3; r++) {
        ahead |= ny == r && (bricks[r] >> bx & 1) != 0;
        beside |= by == r && (bricks[r] >> nx & 1) != 0;
        corner |= ny == r && (bricks[r] >> nx & 1) != 0;
    }
    if (ahead || beside || corner) {
        for (int r = 0; r < 3; r++) {
            if (ahead) { if (ny == r) bricks[r] &= ~((uint8)1 << bx); }
            else if (beside) { if (by == r) bricks[r] &= ~((uint8)1 << nx); }
            else if (ny == r) bricks[r] &= ~((uint8)1 << nx);
        }
        if (!beside || ahead) down_ = !down_;
        if (!ahead) right_ = !right_;
        return;
    }

    // The paddle: its left half sends the ball left, its right half right.
    if (down_ && by == 8) {
        if (bx == paddle || nx == paddle) { down_ = false; right_ = false; return; }
        if (bx == paddle + 1 || nx == paddle + 1) { down_ = false; right_ = true; return; }
    }
    if (ny >= 10) {
        // Missed: serve again, and refill the wall once it is empty.
        bx = 4; by = 6; down_ = false; wait = 3;
        if (bricks[0] == 0 && bricks[1] == 0 && bricks[2] == 0)
            for (int r = 0; r < 3; r++) bricks[r] = 255;
        return;
    }
    bx = nx;
    by = ny;
}

void draw() {
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 8; x++) {
            bool brick = y < 3 && (bricks[y < 3 ? y : 0] >> x & 1) != 0;
            bool pad = y == 9 && (paddle == x || paddle + 1 == x);
            pixel(x, y, brick || pad || (bx == x && by == y));
        }
}
