#ifndef HENGE_HEADLESS_H
#define HENGE_HEADLESS_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Single-CPU, serial-only bring-up. FDs are borrowed and never written to.
   No renderer, restore service, network or audio host backend is attached. */
typedef struct {
    int rom_fd, disk_fd, aux_fd;
    uint64_t ecid, seed, aux_offset;
    unsigned ram_mb, timeout_ms;
    int selftest;
    void (*serial_output)(void *opaque, uint8_t byte);
    void *serial_opaque;
} HvHeadlessConfig;
/* Caller serializes engine use and initializes cancel to zero before starting.
   0: guest stopped normally, 1: timeout/cancel, -1: setup or guest failure.
   Self-test returns 0 only when its serial, RAM and disk checks all pass. */
int hv_headless_run(const HvHeadlessConfig *cfg, _Atomic int *cancel,
                    char *report, size_t capacity);
extern const unsigned char hv_serial_rom[], hv_serial_rom_end[];
extern const unsigned char hv_serial_rom_stop[];
#endif
