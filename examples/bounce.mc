// Bounce: a ball moves by itself and bounces off the walls.
// Press "pause" to stop and start it.
#display 8 6
input press pause;

uint3 x = 1;
uint3 y = 2;
bool goRight = true;   // the direction is part of the state too
bool goDown = true;
bool paused = false;

void tick() {
    if (pause) paused = !paused;
    if (paused) return;
    // Turn around at the walls, then take one step.
    if (x == 7) goRight = false;
    if (x == 0) goRight = true;
    if (y == 5) goDown = false;
    if (y == 0) goDown = true;
    if (goRight) x++; else x--;
    if (goDown) y++; else y--;
}

void draw() {
    pixel(x, y, 1);
}
