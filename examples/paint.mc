// Paint: arrow keys move the cursor, paint toggles the pixel under it.
#display 6 6
input press left;
input press right;
input press up;
input press down;
input press paint;

uint3 x = 2;
uint3 y = 2;
bool canvas[36];
bool blink = false;

void tick() {
    if (paint) canvas[(uint6)y * 6 + x] = !canvas[(uint6)y * 6 + x];
    if (left && x > 0) x--;
    if (right && x < 5) x++;
    if (up && y > 0) y--;
    if (down && y < 5) y++;
    blink = !blink;
}

void draw() {
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 6; i++)
            pixel(i, j, canvas[j * 6 + i] ^ (blink && x == i && y == j));
}
