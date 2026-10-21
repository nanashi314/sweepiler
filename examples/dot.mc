// Dot: move a dot with the arrow keys. It stops at the edges.
#display 8 8
input hold left;     // "hold": true for every tick the key is down
input hold right;
input hold up;
input hold down;

uint3 x = 3;         // 3 bits: 0..7, one value per column
uint3 y = 3;

void tick() {
    if (left && x > 0) x--;
    if (right && x < 7) x++;
    if (up && y > 0) y--;
    if (down && y < 7) y++;
}

void draw() {
    pixel(x, y, 1);
}
