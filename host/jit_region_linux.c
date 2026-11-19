/* Linux/Android: shared backing with separate RW and RX views. No mapping is
   ever both writable and executable. If the kernel denies executable memfd
   mappings, report failure; do not weaken SELinux or request root. */
#include "jit_region.h"
#include <errno.h>
#include <limits.h>
#include <linux/memfd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

int hv_jit_region_attach(HvJitRegion *r, void *rx, void *rw, size_t bytes) {
    memset(r, 0, sizeof *r);
    if (!rx || !rw || bytes < 8 || bytes % 4) { errno = EINVAL; return -1; }
    r->base = rx;
    r->rw_diff = (intptr_t)((uintptr_t)rw - (uintptr_t)rx);
    r->capacity = bytes / 4;
    return 0;
}

int hv_jit_region_open(HvJitRegion *r, size_t bytes) {
    memset(r, 0, sizeof *r);
    if (bytes < 8 || bytes % 4 || bytes > PTRDIFF_MAX) { errno = EINVAL; return -1; }
    int fd = (int)syscall(SYS_memfd_create, "henge-jit", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)bytes)) { int e = errno; close(fd); errno = e; return -1; }
    void *rw = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (rw == MAP_FAILED) { int e = errno; close(fd); errno = e; return -1; }
    void *rx = mmap(NULL, bytes, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    int e = errno;
    close(fd);
    if (rx == MAP_FAILED) { munmap(rw, bytes); errno = e; return -1; }
    hv_jit_region_attach(r, rx, rw, bytes);
    r->owned = 1;
    return 0;
}

void hv_jit_region_close(HvJitRegion *r) {
    if (r->owned && r->base) {
        size_t bytes = r->capacity * 4;
        munmap((void *)((uintptr_t)r->base + (uintptr_t)r->rw_diff), bytes);
        munmap(r->base, bytes);
    }
    memset(r, 0, sizeof *r);
}

void hv_jit_write(HvJitRegion *r, uint32_t *start, const void *src, size_t bytes) {
    void *rw = (void *)((uintptr_t)start + (uintptr_t)r->rw_diff);
    if (bytes == 4) {
        uint32_t word;
        memcpy(&word, src, 4);
        __atomic_store_n((uint32_t *)rw, word, __ATOMIC_RELEASE);
    } else memcpy(rw, src, bytes);
}

void hv_jit_finish(HvJitRegion *r, uint32_t *start, size_t words) {
    if (!start || !words) return;
    char *rx = (char *)start, *rw = (char *)((uintptr_t)start + (uintptr_t)r->rw_diff);
    /* Clean through the alias that received the writes, then invalidate the
       executable alias. Compiler runtime uses this CPU's cache-line sizes. */
    __builtin___clear_cache(rw, rw + words * 4);
    __builtin___clear_cache(rx, rx + words * 4);
}
void hv_jit_finish2(HvJitRegion *r, uint32_t *a, size_t na, uint32_t *b, size_t nb) {
    hv_jit_finish(r, a, na);
    hv_jit_finish(r, b, nb);
}
void hv_jit_finish_branch(HvJitRegion *r, uint32_t *word) { hv_jit_finish(r, word, 1); }

uint32_t *hv_jit_reserve(HvJitRegion *r, size_t words) {
    if (words > SIZE_MAX - 1) return NULL;
    words = (words + 1) & ~(size_t)1;
    size_t cap = r->limit && r->limit < r->capacity ? r->limit : r->capacity;
    size_t at = __atomic_load_n(&r->used, __ATOMIC_RELAXED);
    do {
        if (at > cap || words > cap - at) return NULL;
    } while (!__atomic_compare_exchange_n(&r->used, &at, at + words, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return r->base + at;
}
void hv_jit_reset(HvJitRegion *r) { r->used = 0; }
