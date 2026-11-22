#include "headless.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void *cancel_later(void *p) { usleep(100000); atomic_store((_Atomic int *)p, 1); return NULL; }
int main(void) {
    char disk_path[] = "/tmp/henge-disk-XXXXXX", rom_path[] = "/tmp/henge-rom-XXXXXX";
    int disk = mkstemp(disk_path), rom = mkstemp(rom_path);
    CHECK(disk >= 0 && rom >= 0);
    unlink(disk_path); unlink(rom_path);
    unsigned char sector[512] = "HENG test sector";
    CHECK(write(disk, sector, sizeof sector) == sizeof sector);
    char report[40000]; _Atomic int cancel = 0;
    HvHeadlessConfig cfg = {.rom_fd=rom, .disk_fd=disk, .aux_fd=disk,
        .ram_mb=64, .timeout_ms=2000, .selftest=1};
    for (int i=0;i<3;i++) {
        CHECK(hv_headless_run(&cfg, &cancel, report, sizeof report) == 0);
        CHECK(strstr(report,"HENGE: RAM + DISK + UART PASS"));
    }
    puts(report);
    /* Actual file-based ROM path, not the embedded self-test path. Infinite
       branch exercises asynchronous stop of translated code. */
    uint32_t loop = 0x14000000;
    CHECK(write(rom,&loop,4) == 4);
    cfg.selftest=0; cfg.timeout_ms=50;
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == 1);
    CHECK(strstr(report,"batas waktu"));
    cfg.timeout_ms=5000;
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,cancel_later,&cancel) == 0);
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == 1);
    pthread_join(thread,NULL);
    CHECK(strstr(report,"dibatalkan"));
    atomic_store(&cancel,0);
    /* Invalid inputs must fail without leaving descriptors/threads alive. */
    cfg.aux_offset=512;
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == -1);
    cfg.aux_offset=0;
    CHECK(ftruncate(rom,0) == 0);
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == -1);
    CHECK(ftruncate(rom,(1<<20)+4) == 0);
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == -1);
    int pipefd[2]; CHECK(pipe(pipefd) == 0);
    cfg.disk_fd=pipefd[0];
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == -1);
    close(pipefd[0]); close(pipefd[1]); cfg.disk_fd=disk;
    /* A wrong disk must fail the guest-generated test, not print a host PASS. */
    sector[0]='X'; CHECK(pwrite(disk,sector,512,0) == 512);
    cfg.selftest=1;
    CHECK(hv_headless_run(&cfg,&cancel,report,sizeof report) == -1);
    CHECK(strstr(report,"HENGE: SELFTEST FAIL"));
    unsigned char unchanged[512]; CHECK(pread(disk,unchanged,512,0) == 512);
    CHECK(memcmp(sector,unchanged,512) == 0);
    close(disk); close(rom);
    puts("Headless lifecycle, guest disk/UART, timeout, cancellation and invalid-file checks passed.");
    return 0;
}
