// Pong: keep the ball in play with the paddle on the left (up / down).
#display 8 6
input hold up;
input hold down;

uint3 paddle = 2;
uint3 bx = 4;
uint3 by = 2;
bool dx = false;    // moving right
bool dy = true;     // moving down
bool slow = false;
uint2 lost = 0;

void tick() {
    if (up && paddle > 0) paddle--;
    else if (down && paddle < 4) paddle++;
    slow = !slow;
    if (lost > 0) {
        lost--;
        bx = 4;
        by = 2;
    } else if (slow) {
        if (by == 0) dy = true;
        else if (by == 5) dy = false;
        if (bx == 7) dx = false;
        else if (bx == 1 && (by == paddle || by == paddle + 1)) dx = true;
        if (bx == 0) lost = 3;
        else {
            if (dx) bx++; else bx--;
            if (dy) by++; else by--;
        }
    }
}

void draw() {
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 8; i++)
            pixel(i, j, (bx == i && by == j) || (i == 0 && (paddle == j || paddle + 1 == j)) || (lost > 0 && j == 0));
}
