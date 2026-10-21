// Invaders: move with left / right and fire at the marching rows.
#display 8 8
input hold left;
input hold right;
input press fire;

uint8 rows[2] = {0x3C, 0x3C};   // the invaders; bit x is column x
uint3 top = 0;                  // row of the first invader row
bool east = true;               // marching direction
uint3 march = 0;                // they step every eighth tick
uint3 player = 3;               // on row 7
uint3 sx = 0;                   // the shot
uint3 sy = 0;
bool shot = false;

void restart() {
    rows[0] = 0x3C; rows[1] = 0x3C;
    top = 0; east = true; shot = false;
}

void tick() {
    if (left && player > 0) player--;
    else if (right && player < 7) player++;
    if (fire && !shot) { shot = true; sx = player; sy = 6; }

    // The shot flies up one row a tick and removes the invader it reaches.
    if (shot) {
        for (int r = 0; r < 2; r++) {
            bool there = sy == top + r && (rows[r] >> sx & 1) != 0;
            if (there) { rows[r] &= ~((uint8)1 << sx); shot = false; }
        }
        if (shot) { if (sy == 0) shot = false; else sy--; }
    }

    // The block marches sideways and steps down at the edges.
    march++;
    if (march == 0) {
        uint8 all = rows[0] | rows[1];
        if (east && (all & 0x80) != 0) { east = false; top++; }
        else if (!east && (all & 1) != 0) { east = true; top++; }
        else if (east) { rows[0] <<= 1; rows[1] <<= 1; }
        else { rows[0] >>= 1; rows[1] >>= 1; }
    }
    // Cleared, or landed on the player's row: a new wave.
    if ((rows[0] | rows[1]) == 0 || top >= 5) restart();
}

void draw() {
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            bool invader = (top == j && (rows[0] >> i & 1) != 0) || (top + 1 == j && (rows[1] >> i & 1) != 0);
            bool ship = j == 7 && player == i;
            pixel(i, j, invader || ship || (shot && sx == i && sy == j));
        }
}
