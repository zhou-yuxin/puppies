#ifndef PWM_H
#define PWM_H

#include <8051.h>
#include <stdint.h>

// which timer to use, 0 or 1
#define PWM_TIMER_NO            1

// which interrupt to use,
// usually timer 0 -> int 1, and timer 1 -> int 3
#define PWM_INTERRUPT_NO        3

// PWM use high interrupt prority or not
#define PWM_INTERRUPT_PRIORITY  1

// the port to output PWM
#define PWM_PORT                P1

// the pins of each channel, at most 8 channels
#define PWM_PINS                {0, 1, 2, 3}

// define the data type of PWM ticks,
// recommand to use uint8_t if period is less than 255 ticks.
// typedef uint8_t pwm_ticks_t;
typedef uint16_t pwm_ticks_t;

// initialize PWM module.
// grain is the timer ticks of the min unit in PWM modules,
// for example, grain = 100 means a timer interrupt is triggered
// every 100 timer ticks.
// if period = 200, then the period of all channels are
// 200 * grain timer ticks.
void pwm_init(uint8_t grain, pwm_ticks_t period);

// set the width (high level time) of a channel.
// if width = 10, then the length of high level is
// 10 * grain timer ticks
void pwm_set(uint8_t channel, pwm_ticks_t width);

// apply the changes of pwm_set().
void pwm_apply();

// sdcc requires that a prototype of the ISR must be present
// or included in the file that contains the function main().
// so the file contains main() should include this header file,
// and user shouldn't call it manually.
void pwm_on_timer() __interrupt(PWM_INTERRUPT_NO);

#endif