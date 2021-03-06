#ifndef _TIME_H_
#define _TIME_H_

#include <stdint.h>
#include <stdlib.h>

#include <sys/time.h>

extern uint64_t tsc_hz;

static inline uint64_t rdtsc(void)
{
        uint32_t hi, lo;
        __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
        return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline double tsc_to_us(uint64_t cycles)
{
	return cycles * 1000000.0 / tsc_hz;
}

/* Return current time in seconds since the Epoch. 
 * This is consistent with Python's time.time() */
static inline double get_epoch_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}

#endif
