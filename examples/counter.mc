// A four-digit decimal counter in a 3 x 5 font. Start pauses and resumes,
// reset sets it back to 0000.
#display 15 7
input press start;
input press reset;

uint4 digits[4];       // most significant first
bool running = true;

// The 3 x 5 picture of a digit: bit row * 3 + column.
uint15 glyph(uint4 n) {
    return n == 0 ? 0x7B6F : n == 1 ? 0x749A : n == 2 ? 0x73E7 : n == 3 ? 0x79E7 : n == 4 ? 0x49ED
         : n == 5 ? 0x79CF : n == 6 ? 0x7BCF : n == 7 ? 0x4927 : n == 8 ? 0x7BEF : 0x79EF;
}

void tick() {
    if (start) running = !running;
    if (reset) {
        for (int i = 0; i < 4; i++) digits[i] = 0;
        return;
    }
    if (!running) return;
    // Add one, carrying from the last digit to the first.
    bool carry = true;
    for (int i = 3; i >= 0; i--) {
        if (carry) {
            if (digits[i] == 9) digits[i] = 0;
            else { digits[i]++; carry = false; }
        }
    }
}

void draw() {
    for (int k = 0; k < 4; k++) {
        uint15 g = glyph(digits[k]);
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                pixel(k * 4 + col, 1 + row, g >> (row * 3 + col) & 1);
    }
}
