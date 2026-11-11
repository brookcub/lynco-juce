// Test-only: makes CLOCK_MONOTONIC read as CLOCK_MONOTONIC_RAW in the process that preloads it, so
// the wiring of the JACK processing-cycle role can be exercised on a jackd2 whose clock is
// CLOCK_MONOTONIC_RAW. This does not prove anything about real clock agreement.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <time.h>

int clock_gettime (clockid_t id, struct timespec* ts)
{
    static int (*real) (clockid_t, struct timespec*);
    if (! real) real = (int (*) (clockid_t, struct timespec*)) dlsym (RTLD_NEXT, "clock_gettime");
    return real (id == CLOCK_MONOTONIC ? CLOCK_MONOTONIC_RAW : id, ts);
}
