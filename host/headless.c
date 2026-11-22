#include "headless.h"
#include "vmapple.h"
#include "dispatch.h"
#include "platform.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROM_LIMIT (1u << 20)
#define SERIAL_LIMIT 32768

typedef struct {
    HvMachine machine;
    HvVmapple board;
    HvCpu *cpu;
    HvDisk disk, aux;
    _Atomic int done;
    _Atomic int *cancel;
    void (*live_output)(void *, uint8_t);
    void *live_opaque;
    int exit_reason, stopped;
    char serial[SERIAL_LIMIT];
    size_t serial_size;
    int serial_truncated;
} Session;

static int disk_fd(HvDisk *d, int fd, uint64_t offset) {
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size <= offset || ((uint64_t)st.st_size - offset) % 512) {
        errno = EINVAL;
        return -1;
    }
    d->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (d->fd < 0) return -1;
    d->wfd = -1;
    d->offset = offset;
    d->size = (uint64_t)st.st_size - offset;
    d->readonly = 1;
    return 0;
}

static int read_rom(Session *s, int fd) {
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 4 || st.st_size > ROM_LIMIT || st.st_size % 4) return -1;
    size_t at = 0, len = (size_t)st.st_size;
    while (at < len) {
        ssize_t n = pread(fd, s->board.rom + at, len - at, (off_t)at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        at += (size_t)n;
    }
    return 0;
}

static void serial_output(void *opaque, uint8_t c) {
    Session *s = opaque;
    /* Keep JNI's report valid ASCII and bounded, including binary firmware output. */
    if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c > 126)) c = '.';
    if (s->serial_size + 1 < sizeof s->serial) {
        s->serial[s->serial_size++] = (char)c;
        s->serial[s->serial_size] = 0;
    } else s->serial_truncated = 1;
    if (s->live_output) s->live_output(s->live_opaque, c);
}

static void *run_cpu(void *arg) {
    Session *s = arg;
    /* Never run guest code on an ART/Java thread: translated code uses guest SP.
       This owned native thread blocks asynchronous signals; no process-wide
       handlers are changed and the VM is destroyed only after it is joined. */
    sigset_t blocked;
    sigfillset(&blocked);
    sigdelset(&blocked, SIGSEGV); sigdelset(&blocked, SIGBUS);
    sigdelset(&blocked, SIGILL); sigdelset(&blocked, SIGFPE); sigdelset(&blocked, SIGTRAP);
    pthread_sigmask(SIG_BLOCK, &blocked, NULL);
    void *signal_memory = malloc(64u << 10);
    stack_t alt = {.ss_sp = signal_memory, .ss_size = 64u << 10};
    if (!signal_memory || sigaltstack(&alt, NULL)) {
        free(signal_memory);
        s->exit_reason = HV_EXIT_UNSUPPORTED;
        atomic_store(&s->done, 1);
        return NULL;
    }
    s->exit_reason = hv_run(s->cpu);
    stack_t disabled = {.ss_flags = SS_DISABLE};
    sigaltstack(&disabled, NULL);
    free(signal_memory);
    atomic_store(&s->done, 1);
    return NULL;
}

int hv_headless_run(const HvHeadlessConfig *cfg, _Atomic int *cancel, char *out, size_t cap) {
    if (!out || !cap) return -1;
    out[0] = 0;
    if (!cfg || !cancel || cfg->ram_mb < 64 || cfg->ram_mb > 2048 ||
        cfg->timeout_ms < 10 || cfg->timeout_ms > 30000) {
        snprintf(out, cap, "Konfigurasi tidak valid: RAM 64-2048 MiB, waktu 10-30000 ms.");
        return -1;
    }
    Session *s = calloc(1, sizeof *s);
    if (!s) { snprintf(out, cap, "Memori host tidak cukup."); return -1; }
    s->board.ram_fd = s->disk.fd = s->disk.wfd = s->aux.fd = s->aux.wfd = -1;
    s->cancel = cancel;
    s->live_output = cfg->serial_output;
    s->live_opaque = cfg->serial_opaque;
    s->cpu = aligned_alloc(_Alignof(HvCpu), sizeof(HvCpu));
    const char *error = NULL;
    int rc = -1, machine_ready = 0, board_ready = 0;
    if (!s->cpu) { error = "Alokasi CPU gagal."; goto cleanup; }
    if (disk_fd(&s->disk, cfg->disk_fd, 0)) { error = "Disk harus file lokal raw, tidak kosong, kelipatan 512 byte."; goto cleanup; }
    if (!cfg->selftest && disk_fd(&s->aux, cfg->aux_fd, cfg->aux_offset)) {
        error = "Auxiliary storage tidak valid atau tidak dapat dibaca."; goto cleanup;
    }
    if (atomic_load(cancel)) { rc = 1; error = "Dibatalkan sebelum VM dibuat."; goto cleanup; }
    machine_ready = 1;
    if (hv_machine_init(&s->machine, 32u << 20)) { error = "Alokasi mesin/JIT gagal atau CPU tidak didukung."; goto cleanup; }
    HvCfgParams identity = {.nr_cpus = 1, .ecid = cfg->ecid, .seed = cfg->seed};
    board_ready = 1;
    if (hv_vmapple_create(&s->board, &s->machine, s->cpu, (uint64_t)cfg->ram_mb << 20,
                         &s->disk, cfg->selftest ? NULL : &s->aux, &identity, NULL)) {
        error = "Pembuatan board atau pemetaan RAM gagal."; goto cleanup;
    }
    if (cfg->selftest) {
        memcpy(s->board.rom, hv_serial_rom, (size_t)(hv_serial_rom_end - hv_serial_rom));
        s->machine.stop_pc = VMAPPLE_FIRMWARE + (uint64_t)(hv_serial_rom_stop - hv_serial_rom);
    } else if (read_rom(s, cfg->rom_fd)) {
        error = "ROM harus file raw ARM64, 4 byte sampai 1 MiB, ukuran kelipatan 4."; goto cleanup;
    }
    s->board.uart->output = serial_output;
    s->board.uart->output_opaque = s;
    hv_vmapple_reset_cpu(&s->board);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int thread_error = pthread_attr_setstacksize(&attr, 8u << 20);
    pthread_t worker;
    if (!thread_error) thread_error = pthread_create(&worker, &attr, run_cpu, s);
    pthread_attr_destroy(&attr);
    if (thread_error) { error = "Thread CPU gagal dimulai."; goto cleanup; }
    uint64_t start = hv_host_ticks();
    while (!atomic_load(&s->done)) {
        int stop = atomic_load(cancel) ? 1 : hv_host_ticks() - start >= (uint64_t)cfg->timeout_ms * 24000 ? 2 : 0;
        if (stop) {
            s->stopped = stop;
            hv_machine_halt(&s->machine, HV_HALT_HOST);
            break;
        }
        hv_machine_kick_all(&s->machine);
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    pthread_join(worker, NULL);
    int passed = !s->stopped && s->exit_reason == HV_EXIT_HALT &&
                 strstr(s->serial, "HENGE: RAM + DISK + UART PASS\n") != NULL;
    rc = s->stopped ? 1 : (cfg->selftest ? (passed ? 0 : -1) :
         (s->exit_reason == HV_EXIT_HALT && s->cpu->halted == HV_HALT_SHUTDOWN ? 0 : -1));
    snprintf(out, cap,
        "%s\nRAM guest: %u MiB; CPU: 1; JIT: 32 MiB\nDisk root: %llu byte (hanya baca)\n"
        "GIC, timer, UART, PCIe, BDIF, virtio-blk, AES: terpasang\nGPU/renderer, audio host, jaringan, installer: belum tersedia\n"
        "Berhenti: %s; exit=%d; halt=%d\nPC guest: 0x%llx; EL%u\n"
        "--- SERIAL GUEST ---\n%s%s\n--- AKHIR SERIAL ---\n"
        "Ini belum membuktikan macOS berhasil boot.",
        cfg->selftest ? (passed ? "Uji board: LULUS" : "Uji board: GAGAL") : "Percobaan boot ROM selesai",
        cfg->ram_mb, (unsigned long long)s->disk.size,
        s->stopped == 1 ? "dibatalkan" : s->stopped == 2 ? "batas waktu" : "guest berhenti",
        s->exit_reason, (int)s->cpu->halted, (unsigned long long)s->cpu->pc, s->cpu->el,
        s->serial_size ? s->serial : "(belum ada keluaran UART)\n",
        s->serial_truncated ? "\n[log serial mencapai batas 32 KiB]" : "");
cleanup:
    if (error) snprintf(out, cap, "%s", error);
    if (board_ready) hv_vmapple_destroy(&s->board);
    if (machine_ready) hv_machine_free(&s->machine);
    hv_disk_close(&s->disk); hv_disk_close(&s->aux);
    free(s->cpu); free(s);
    return rc;
}
