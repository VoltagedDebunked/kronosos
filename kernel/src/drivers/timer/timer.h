#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

// PIT (Programmable Interval Timer) constants
#define PIT_FREQUENCY       1193182    // The base frequency of the PIT (1.193182 MHz)
#define PIT_CHANNEL0        0x40       // Channel 0 data port
#define PIT_CHANNEL1        0x41       // Channel 1 data port
#define PIT_CHANNEL2        0x42       // Channel 2 data port
#define PIT_COMMAND         0x43       // Command register port

// Timer functions
void timer_init(uint32_t frequency);
void timer_set_frequency(uint32_t frequency);
uint64_t timer_get_ticks(void);
void timer_sleep(uint32_t ms);
uint64_t timer_get_uptime_ms(void);

// Timer callback registration
typedef void (*timer_callback_t)(uint64_t tick_count);
void timer_register_callback(timer_callback_t callback);

#endif // TIMER_H