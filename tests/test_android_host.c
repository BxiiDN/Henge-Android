#include "android_probe.h"
#include "jit_region.h"
#include "platform.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    CHECK(hv_ticks_from_timespec((struct timespec){1, 0}) == 24000000);
    CHECK(hv_ticks_from_timespec((struct timespec){0, 999999999}) == 23999999);
    CHECK(hv_ticks_from_timespec((struct timespec){1000000000, 500000000}) == 24000000012000000ull);
    pthread_cond_t cv;
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    CHECK(hv_host_cond_init(&cv) == 0);
    CHECK(pthread_mutex_lock(&mu) == 0);
    struct timespec duration = {0, 2000000};
    uint64_t start = hv_host_ticks();
    CHECK(hv_host_cond_wait(&cv, &mu, &duration) == ETIMEDOUT);
    CHECK(hv_host_ticks() - start >= 48000);
    pthread_mutex_unlock(&mu);
    pthread_cond_destroy(&cv);
    pthread_mutex_destroy(&mu);

    HvJitRegion region;
    CHECK(hv_jit_region_open(&region, 7) == -1 && errno == EINVAL);
    CHECK(hv_jit_region_open(&region, 1024) == 0);
    uint32_t *code = hv_jit_reserve(&region, 2);
    CHECK(code != NULL);
    const uint32_t body[] = {0xd2800540, 0xd65f03c0};
    hv_jit_write(&region, code, body, sizeof body);
    hv_jit_finish(&region, code, 2);
    long (*fn)(void) = (long (*)(void))code;
    CHECK(fn() == 42);
    uint32_t patch = 0xd2800560; /* mov x0,43 */
    hv_jit_write(&region, code, &patch, sizeof patch);
    hv_jit_finish_branch(&region, code);
    CHECK(fn() == 43);
    size_t used = region.used;
    CHECK(hv_jit_reserve(&region, SIZE_MAX) == NULL);
    CHECK(hv_jit_reserve(&region, 1024) == NULL);
    CHECK(region.used == used);
    hv_jit_region_close(&region);
    char report[1024];
    for (int i = 0; i < 3; i++) {
        CHECK(hv_android_cpu_probe(report, sizeof report) == 0);
        CHECK(strstr(report, "LULUS") != NULL);
    }
    puts(report);
    return 0;
}
