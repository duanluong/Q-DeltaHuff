#ifndef BENCH_TIMER_H
#define BENCH_TIMER_H

#include <stdint.h>

/*
 * High resolution monotonic timer, the PC equivalent of the DWT->CYCCNT
 * (STM32) / CCOUNT (ESP32) cycle counters used by the firmware. Values are
 * opaque ticks: subtract two readings, then convert with
 * bench_timer_ticks_to_us().
 */
void     bench_timer_init(void);
uint64_t bench_timer_now(void);
double   bench_timer_ticks_to_us(uint64_t ticks);
double   bench_timer_resolution_ns(void);

#endif /* BENCH_TIMER_H */
