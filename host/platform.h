#ifndef HENGE_HOST_PLATFORM_H
#define HENGE_HOST_PLATFORM_H

#include <stdint.h>
#include <pthread.h>
#include <time.h>

/* Keep the guest's 24 MHz counter contract, independently of the host timer.
   Integer quotient/remainder avoids multiplying an uptime in nanoseconds. */
static inline uint64_t hv_ticks_from_timespec(struct timespec t) {
    return (uint64_t)t.tv_sec * 24000000ull + (uint64_t)t.tv_nsec * 3 / 125;
}

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#define HV_HOST_WFE_PROBE (-1)
static inline uint64_t hv_host_ticks(void) { return mach_absolute_time(); }
static inline int hv_host_cond_init(pthread_cond_t *cv) { return pthread_cond_init(cv, NULL); }
static inline int hv_host_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mu, const struct timespec *duration) {
    return pthread_cond_timedwait_relative_np(cv, mu, duration);
}
extern int __ulock_wait(uint32_t, void *, uint64_t, uint32_t);
extern int __ulock_wake(uint32_t, void *, uint64_t);
static inline void hv_host_wait_word(void *word, uint32_t expected, uint32_t timeout_us) {
    __ulock_wait(1u | 0x1000000u, word, expected, timeout_us);
}
static inline void hv_host_wake_word(void *word) {
    __ulock_wake(1u | 0x100u | 0x1000000u, word, 0);
}
#elif defined(__linux__)
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
/* Linux does not promise a periodic EL0 event stream. Never probe with WFE. */
#define HV_HOST_WFE_PROBE 0
static inline uint64_t hv_host_ticks(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
    return hv_ticks_from_timespec(t);
}
static inline int hv_host_cond_init(pthread_cond_t *cv) {
    pthread_condattr_t attr;
    int rc = pthread_condattr_init(&attr);
    if (rc) return rc;
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!rc) rc = pthread_cond_init(cv, &attr);
    pthread_condattr_destroy(&attr);
    return rc;
}
static inline int hv_host_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mu, const struct timespec *duration) {
    struct timespec until;
    if (clock_gettime(CLOCK_MONOTONIC, &until)) abort();
    until.tv_sec += duration->tv_sec;
    until.tv_nsec += duration->tv_nsec;
    if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(cv, mu, &until);
}
static inline void hv_host_wait_word(void *word, uint32_t expected, uint32_t timeout_us) {
    struct timespec timeout = {timeout_us / 1000000, (long)(timeout_us % 1000000) * 1000};
    syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, expected, &timeout, NULL, 0);
}
static inline void hv_host_wake_word(void *word) {
    syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}
#else
#error Unsupported Henge host platform
#endif
#endif
