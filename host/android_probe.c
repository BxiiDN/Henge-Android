#include "android_probe.h"
#include "cpu_features.h"
#include "dispatch.h"
#include "platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hv_android_cpu_probe(char *report, size_t capacity) {
    if (!hv_host_cpu_supported()) {
        snprintf(report, capacity, "CPU tidak didukung: membutuhkan ARM64, SIMD, dan LSE.");
        return -1;
    }
    HvMachine *m = calloc(1, sizeof *m);
    HvCpu *cpu = aligned_alloc(_Alignof(HvCpu), sizeof(HvCpu));
    uint32_t *ram = aligned_alloc(16384, 16384);
    if (!m || !cpu || !ram) {
        free(ram); free(cpu); free(m);
        snprintf(report, capacity, "Memori tidak cukup untuk pengujian CPU.");
        return -1;
    }
    if (hv_machine_init(m, 4 << 20)) {
        int error = errno;
        hv_machine_free(m);
        free(ram); free(cpu); free(m);
        snprintf(report, capacity, "Gagal membuat mesin/JIT: %s", strerror(error));
        return -1;
    }
    memset(ram, 0, 16384);
    /* MOV x0,40; ADD x0,x0,2; MRS x1,CNTVCT_EL0; MRS x2,CNTFRQ_EL0 */
    const uint32_t program[] = {0xd2800500, 0x91000800, 0xd53be041, 0xd53be002};
    memcpy(ram, program, sizeof program);
    if (hv_machine_add_ram(m, 0x10000, 16384, ram, 1)) {
        hv_machine_free(m);
        free(ram); free(cpu); free(m);
        snprintf(report, capacity, "Gagal mengalokasikan pemetaan RAM guest.");
        return -1;
    }
    hv_cpu_reset(cpu, m);
    cpu->pc = 0x10000;
    m->stop_pc = cpu->pc + sizeof program;
    uint64_t before = hv_host_ticks();
    int result = hv_run(cpu);
    uint64_t after = hv_host_ticks();
    int ok = result == HV_EXIT_HALT && cpu->x[0] == 42 && cpu->x[2] == 24000000 &&
             cpu->x[1] >= before && cpu->x[1] <= after;
    snprintf(report, capacity,
             "CPU/JIT: %s\nAritmetika: 40 + 2 = %llu\nTimer guest: %llu Hz\nHalaman host: %ld byte\n"
             "Program ARM64 diterjemahkan dan dieksekusi oleh mesin Henge.\n"
             "Hasil ini belum membuktikan macOS dapat boot.",
             ok ? "LULUS" : "GAGAL", (unsigned long long)cpu->x[0],
             (unsigned long long)cpu->x[2], sysconf(_SC_PAGESIZE));
    hv_machine_free(m);
    free(ram); free(cpu); free(m);
    return ok ? 0 : -1;
}
