// Catch: move the paddle to catch the falling ball. A miss flashes the screen.
#display 6 8
input hold left;
input hold right;

uint3 paddle = 2;
uint3 ballx = 1;
uint3 bally = 0;
uint5 rng = 21;
bool slow = false;
uint2 flash = 0;

void tick() {
    rng = rng << 1 | (rng >> 4 ^ rng >> 2) & 1;
    if (left && paddle > 0) paddle--;
    else if (right && paddle < 4) paddle++;
    slow = !slow;
    if (flash > 0) flash--;
    else if (slow) {
        if (bally < 7) bally++;
        else {
            if (ballx != paddle && ballx != paddle + 1) flash = 3;
            bally = 0;
            ballx = (uint3)(rng & 3);
            if (rng >> 4 & 1) ballx += 2;
        }
    }
}

void draw() {
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 8; j++)
            pixel(i, j, flash > 0 || (ballx == i && bally == j) || (j == 7 && (paddle == i || paddle + 1 == i)));
}
