// Blink: the smallest kind of program. A light turns on and off by itself.
// Press "speed" to make it blink slower or faster.
#display 4 4
input press speed;

bool on = false;     // global variables keep their value from tick to tick
bool slow = false;
uint2 wait = 0;      // ticks to wait before the next change

// tick runs once per tick and changes the state.
void tick() {
    if (speed) slow = !slow;
    if (wait > 0) {
        wait--;
        return;
    }
    on = !on;
    wait = slow ? 3 : 0;
}

// draw makes the picture from the state.
void draw() {
    pixel(1, 1, on);
    pixel(2, 1, on);
    pixel(1, 2, on);
    pixel(2, 2, on);
}
