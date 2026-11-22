#ifndef HENGE_HOST_CPU_FEATURES_H
#define HENGE_HOST_CPU_FEATURES_H
#include <stdint.h>
#ifdef __linux__
#include <asm/hwcap.h>
#include <sys/auxv.h>
/* The translator emits LSE instructions itself. Reject earlier CPUs before
   executing it, even if a particular guest does not request LSE. */
static inline int hv_host_cpu_supported(void) {
    unsigned long required = HWCAP_FP | HWCAP_ASIMD | HWCAP_ATOMICS;
    return (getauxval(AT_HWCAP) & required) == required;
}
static inline uint64_t hv_limit_field(uint64_t id, unsigned bit, unsigned maximum) {
    unsigned field = (unsigned)(id >> bit) & 15;
    return field <= maximum ? id : (id & ~(15ull << bit)) | ((uint64_t)maximum << bit);
}
/* Never advertise pass-through instructions absent from this Android/Linux
   CPU. This does not emulate an unsupported opcode executed unconditionally.
   Guest PAuth remains emulated by Henge, independently of host PAuth. */
static inline uint64_t hv_host_isar0(uint64_t id) {
    unsigned long h = getauxval(AT_HWCAP);
    id = hv_limit_field(id, 4, !(h & HWCAP_AES) ? 0 : h & HWCAP_PMULL ? 2 : 1);
    id = hv_limit_field(id, 8, !!(h & HWCAP_SHA1));
    id = hv_limit_field(id, 12, !(h & HWCAP_SHA2) ? 0 : h & HWCAP_SHA512 ? 2 : 1);
    id = hv_limit_field(id, 16, !!(h & HWCAP_CRC32));
    id = hv_limit_field(id, 20, h & HWCAP_ATOMICS ? 2 : 0);
    id = hv_limit_field(id, 28, !!(h & HWCAP_ASIMDRDM));
    id = hv_limit_field(id, 32, !!(h & HWCAP_SHA3));
    id = hv_limit_field(id, 44, !!(h & HWCAP_ASIMDDP));
    id = hv_limit_field(id, 48, !!(h & HWCAP_ASIMDFHM));
    return hv_limit_field(id, 52, !!(h & HWCAP_FLAGM));
}
static inline uint64_t hv_host_isar1(uint64_t id) {
    unsigned long h = getauxval(AT_HWCAP), h2 = getauxval(AT_HWCAP2);
    id = hv_limit_field(id, 0, !!(h & HWCAP_DCPOP));
    id = hv_limit_field(id, 12, !!(h & HWCAP_JSCVT));
    id = hv_limit_field(id, 16, !!(h & HWCAP_FCMA));
    id = hv_limit_field(id, 20, h & HWCAP_ILRCPC ? 2 : h & HWCAP_LRCPC ? 1 : 0);
    id = hv_limit_field(id, 32, !!(h2 & HWCAP2_FRINT));
    return hv_limit_field(id, 36, !!(h & HWCAP_SB));
}
static inline uint64_t hv_host_pfr0(uint64_t id) {
    unsigned long h = getauxval(AT_HWCAP);
    id = hv_limit_field(id, 16, !!(h & HWCAP_FPHP));
    return hv_limit_field(id, 20, !!(h & HWCAP_ASIMDHP));
}
#else
static inline int hv_host_cpu_supported(void) { return 1; }
static inline uint64_t hv_host_isar0(uint64_t id) { return id; }
static inline uint64_t hv_host_isar1(uint64_t id) { return id; }
static inline uint64_t hv_host_pfr0(uint64_t id) { return id; }
#endif
#endif
