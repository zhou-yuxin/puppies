#include <pwm.h>

void delay(uint16_t loop) {
    while (loop--);
}

void main() {
    uint8_t ticks;
    pwm_init(200, 400);
    pwm_set(2, 100);
    pwm_set(3, 250);
    pwm_apply();
    while (1) {
        for (ticks = 20; ticks < 40; ticks++) {
            pwm_set(0, ticks);
            pwm_set(1, ticks);
            pwm_apply();
            delay(50000);
        }
        for (ticks = 40; ticks > 20; ticks--) {
            pwm_set(0, ticks);
            pwm_set(1, ticks);
            pwm_apply();
            delay(50000);
        }
    }
}