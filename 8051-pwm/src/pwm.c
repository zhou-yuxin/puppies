#include <pwm.h>

static const uint8_t g_channel_pins[] = PWM_PINS;

#define CHANNEL_COUNT   (sizeof(g_channel_pins) / sizeof(uint8_t))

static pwm_ticks_t g_period, g_next_time;
static uint8_t g_or_mask, g_and_mask;

static pwm_ticks_t g_channel_times[CHANNEL_COUNT];

static pwm_ticks_t g_times[2 * CHANNEL_COUNT];
static uint8_t g_masks[2 * CHANNEL_COUNT];

static struct {
    uint8_t seek : 4;
    uint8_t tab_idx : 1;
    uint8_t dirty : 1;
}
volatile g_cursor;

#define READ_OFFSET     (g_cursor.tab_idx ? CHANNEL_COUNT : 0)
#define WRITE_OFFSET    (g_cursor.tab_idx ? 0 : CHANNEL_COUNT)

#if PWM_TIMER_NO == 0
#define SET_TMOD    TMOD = (TMOD & 0xF0) | 0x02
#define THx         TH0
#define TLx         TL0
#define PTx         PT0
#define ETx         ET0
#define TRx         TR0
#elif PWM_TIMER_NO == 1
#define SET_TMOD    TMOD = (TMOD & 0x0F) | 0x20
#define THx         TH1
#define TLx         TL1
#define PTx         PT1
#define ETx         ET1
#define TRx         TR1
#endif

#define CONFIG_TIMER(grain)                 \
    do {                                    \
        SET_TMOD;                           \
        THx = 256 - (grain);                \
        TLx = THx;                          \
        PTx = PWM_INTERRUPT_PRIORITY;       \
        ETx = 1;                            \
        TRx = 1;                            \
        EA = 1;                             \
    } while(0)

void pwm_init(uint8_t grain, pwm_ticks_t period) {
    uint8_t i;
    g_period = period;
    g_next_time = 1;
    g_or_mask = 0;
    g_and_mask = 0xff;
    for (i = 0; i < CHANNEL_COUNT; i++) {
        g_or_mask |= 1 << g_channel_pins[i];
        g_channel_times[i] = period;
        g_times[i] = period;
    }
    g_cursor.seek = 0;
    g_cursor.tab_idx = 0;
    g_cursor.dirty = 0;
    PWM_PORT |= g_or_mask;
    CONFIG_TIMER(grain);
}

void pwm_set(uint8_t channel, pwm_ticks_t width) {
    if (channel < CHANNEL_COUNT && width <= g_period) {
        g_channel_times[channel] = width;
    }
}

void pwm_apply() {
    uint8_t offset = WRITE_OFFSET;
    uint8_t i, j;
    pwm_ticks_t time;
    uint8_t mask;
    for (i = 0; i < CHANNEL_COUNT; i++) {
        j = i + offset;
        g_times[j] = g_channel_times[i];
        g_masks[j] = i;
    }
    for (j = CHANNEL_COUNT - 1; j > 0; j--) {
        for (i = 0; i < j; i++) {
            uint8_t a = i + offset;
            uint8_t b = a + 1;
            if (g_times[a] > g_times[b]) {
                time = g_times[a];
                g_times[a] = g_times[b];
                g_times[b] = time;
                mask = g_masks[a];
                g_masks[a] = g_masks[b];
                g_masks[b] = mask;
            }
        }
    }
    j = offset;
    time = g_times[offset];
    mask = 1 << g_channel_pins[g_masks[offset]];
    for (i = 1; i < CHANNEL_COUNT; i++) {
        uint8_t idx = i + offset;
        if (g_times[idx] != time) {
            g_times[j] = time;
            g_masks[j] = ~mask;
            j++;
            time = g_times[idx];
        }
        mask |= 1 << g_channel_pins[g_masks[idx]];
    }
    g_times[j] = time;
    g_masks[j] = ~mask;
    i = CHANNEL_COUNT + offset;
    for (j++ ; j < i; j++) {
        g_times[j] = g_period;
        g_masks[j] = 0xff;
    }
    g_cursor.dirty = 1;
    while (g_cursor.dirty);
}

void pwm_on_timer() __interrupt(PWM_INTERRUPT_NO) {
    uint8_t idx;
    PWM_PORT = (PWM_PORT | g_or_mask) & g_and_mask;
    g_next_time++;
    if (g_next_time == g_period) {
        if (g_cursor.dirty) {
            g_cursor.tab_idx = 1 - g_cursor.tab_idx;
            g_cursor.dirty = 0;
        }
        idx = READ_OFFSET;
        if (g_times[idx] == 0) {
            g_and_mask = g_masks[idx];
            g_cursor.seek = 1;
        }
        else {
            g_and_mask = 0xff;
            g_cursor.seek = 0;
        }
        return;
    }
    if (g_next_time == g_period + 1) {
        g_next_time = 1;
    }
    if (g_cursor.seek < CHANNEL_COUNT) {
        idx = g_cursor.seek + READ_OFFSET;
        if (g_next_time == g_times[idx]) {
            g_and_mask = g_masks[idx];
            g_cursor.seek++;
        }
    }
}