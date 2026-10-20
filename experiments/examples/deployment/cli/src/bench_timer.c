#include "bench_timer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static double s_us_per_tick = 0.0;

void
bench_timer_init (void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    s_us_per_tick = 1e6 / (double) freq.QuadPart;
}

uint64_t
bench_timer_now (void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t) now.QuadPart;
}

#else /* POSIX */
#include <time.h>

static double s_us_per_tick = 1e-3; /* ticks are nanoseconds */

void
bench_timer_init (void)
{
}

uint64_t
bench_timer_now (void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}
#endif

double
bench_timer_ticks_to_us (uint64_t ticks)
{
    return (double) ticks * s_us_per_tick;
}

double
bench_timer_resolution_ns (void)
{
    return s_us_per_tick * 1e3;
}
