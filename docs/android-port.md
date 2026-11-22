# Android port: development status

This fork adds an Android/Linux ARM64 host for Henge's CPU engine and an Android diagnostic app. It does **not** boot, install, or display macOS on Android yet. The original iOS application remains in `ios/`.

Initial device target: Xiaomi Pad 6, Snapdragon 870 / Adreno 650. The user reports that Diagnostics 0.1 passed on a physical Xiaomi 23043RP34C (Pad 6), Android 17/API 37, 7.4 GiB RAM: guest arithmetic 42, 24 MHz timer, 4096-byte host pages, Vulkan 1.1.295 / Adreno 650. This is user-provided device evidence, not a remotely observed test. The new 0.2 board/serial path still needs device execution. The APK targets phones and resizable tablets running Android 9/API 28 or newer, ARM64 with SIMD and LSE atomics. Other ARM64 devices are not automatically compatible. Unsupported CPUs are rejected before entering the translator.

## Version 0.2: serial-only virtual machine

The APK now links the existing vmapple board: GICv3, virtual timer, PL011 UART,
RTC, PCIe, BDIF boot disk, virtio block devices and AES. Linux/Android uses
ordinary mmap RAM, portable disk flushes and the already-vendored Mbed TLS AES
implementation. Apple builds retain CommonCrypto, Mach RAM and Metal paths.
No replacement GPU device or fake renderer is installed in the Android board.

Open **Uji board dan boot serial**, then **Jalankan uji board**. The embedded
project-authored ARM64 test firmware starts at the real vmapple ROM address,
checks a guest RAM store/load, issues a BDIF sector read into guest RAM, checks
the disk bytes, and writes `HENGE: RAM + DISK + UART PASS` through PL011. The
512-byte temporary disk contains test data only and is deleted afterwards.

For an external ROM experiment, select a local raw AVPBooter ROM (4 bytes to
1 MiB, multiple of four), raw root disk and matching auxiliary.bin. Enter the
original VM's ECID and seed. The auxiliary header offset is 16 KiB. Files must
be regular, seekable files; disk payloads must be nonempty multiples of 512
bytes. Android's document picker grants access only to the selected files.
Descriptors stay open for the run, are duplicated by native code and closed
after workers join. Disk and auxiliary storage are **read-only**. Guests that
need persistent writes can fail; this is a bring-up experiment, not a usable
macOS session or installer.

External runs have one virtual CPU, 512/1024/2048 MiB RAM, 32 MiB JIT, a 30-second
execution limit, a stop button and up to 32 KiB of live UART output. Returning
to the background requests stop; rotation preserves the task and report.
CPU execution uses an owned native thread with asynchronous signals blocked
and an alternate signal stack, not the Java UI/ART thread. Teardown joins the
CPU and drains disk workers before unmapping guest RAM. Setup and file-provider
I/O are outside the guest execution deadline. VM halt reason, final PC and EL
are included even if the firmware emits no UART output.

0.2 uses application ID `io.github.bxiidn.henge.serial` and label **Henge Serial
Dev**, allowing installation beside Diagnostics 0.1. Development builds are
debug signed. No Apple ROM, macOS disk or auxiliary image is bundled, and no
Apple firmware/macOS boot has been verified on this Android port.

Validation for 0.2: 11 CTest groups pass under GCC/Linux AArch64 and QEMU 10.0.13.
The board suite performs 143 checks including interrupt delivery, guest UART,
BDIF, virtio block, balloon and AES-128/192/256 encrypt/decrypt plus chained CBC
known-answer vectors. The new lifecycle test runs the embedded ROM three times,
checks a wrong disk really fails, boots an infinite-loop ROM from a file and
verifies timeout and cancellation, rejects empty/oversized ROMs, invalid aux
offsets and pipe descriptors, and verifies the root image remains unchanged.
Android APK assembly and lint are also checked. These are host/build checks;
0.2 physical-device validation remains outstanding.

## Implemented

- Android NDK/CMake builds of the existing translator, MMU, exception handling and CPU dispatcher.
- ELF assembly symbols and relocations, with the existing Mach-O paths retained.
- Linux `memfd` JIT backing with separate RW and RX mappings, cache synchronization and bounded allocation. Mapping failure is reported. No root or SELinux changes are requested.
- Monotonic host time converted to the guest's 24 MHz counter, Linux futex wakeups, monotonic condition waits, and no potentially indefinite Linux WFE probe.
- Preservation of guest SIMD/FP state around Linux C helper calls. Android's reserved host x18 is retained.
- Host CPU feature masking for guest pass-through features. This does not supply missing instruction implementations when guest software executes an unsupported instruction unconditionally.
- An Android app that executes a real, small Henge guest program and reads the device's Vulkan properties. It supports rotation, tablets, copy and user-initiated sharing of results. It has no network or storage permissions.

The diagnostic program runs `40 + 2`, reads CNTVCT/CNTFRQ through Henge, and checks both the result and timer contract. Vulkan enumeration is **not** a rendering test. The guest still uses Henge's own translation engine; QEMU is only a Linux test runner on an x86 development host.

## Build the APK

Install JDK 17, Android SDK 35, build-tools 35.0.0, NDK 27.3.13750724 and CMake 3.22.1. Configure `ANDROID_HOME` or an ignored `android/local.properties` with `sdk.dir=...`.

```sh
cd android
bash gradlew assembleDebug lintDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`. This is a debug-signed diagnostic APK, not a macOS emulator release. The build uses 16 KB ELF alignment and flexible Android page sizes. Physical 4 KB and 16 KB host testing remains outstanding.

GitHub Actions builds the APK and uploads it as a workflow artifact. Installation is a normal APK installation, with no flashing or repartitioning. Open the app and choose **Jalankan uji perangkat** to obtain an actual CPU/JIT and Vulkan report.

## Engine tests

On ARM64 Linux with a CPU supporting the instructions used by the native reference tests:

```sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j4
ulimit -s 65520
ctest --test-dir build/linux --output-on-failure
```

For an x86 Linux development host, install `gcc-aarch64-linux-gnu` and QEMU user-mode 10.0 or newer:

```sh
cmake -S . -B build/linux-arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux-arm64.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-arm64 -j4
QEMU_CPU=max QEMU_STACK_SIZE=67108864 ctest --test-dir build/linux-arm64 --output-on-failure
```

The reference instruction tests use newer ARM ISA instructions and are not intended to run unmodified on every phone. The app runs a separate bounded CPU probe.

Local validation: all 9 CTest groups passed under QEMU AArch64 with a static Linux/musl Debug build, including classifier, operands, JIT, memory, exceptions, JIT mappings, MMU, shutdown and the Android host probe. The host probe also passed using QEMU's Cortex-A76 model. The tests exposed signed-shift undefined behavior and SIMD clobbering, which are fixed here. The DC ZVA test uses the guest's specified 64-byte granule independently of the host granule. Android NDK compilation and APK assembly succeeded, with APK v2 signature and 16 KB ZIP/ELF alignment verified. All seven modified assembly sources also compiled to Apple Mach-O, but a full Apple build/runtime was not available. These checks do not replace physical Android execution.

GCC Linux builds link `libatomic` for 128-bit compare/exchange. QEMU 8.2.2 crashes internally (`QEMU internal SIGSEGV`) while running the glibc-linked Android host probe. The same GCC-built binary passes on QEMU 10.0.13. CI uses Ubuntu 26.04 and checks QEMU >= 10.0, with all tests enabled. This host-emulator result does not establish physical Android compatibility.

## Remaining work

| Component | Current state | Next acceptance check |
| --- | --- | --- |
| CPU/JIT on physical Android | Diagnostics 0.1 passed according to the user's Pad 6 report | Run 0.2 board/serial self-test on the same device |
| Machine/devices | Serial-only vmapple board linked; test firmware passes RAM/disk/UART | Run matching Apple ROM/disk/aux with identity and inspect serial; macOS boot unverified |
| GPU | `devices/pvg_metal.m` requires Apple Metal APIs | Implement a Vulkan backend for the guest command protocol and validate command traces |
| Guest shaders/pipelines | Apple AIR/metallib and serialized Metal pipeline assumptions remain | Translate supported guest shaders to SPIR-V and validate pipeline state/resource semantics |
| Display/input | Diagnostic and serial Activities; virtual input devices have no Android UI binding | Android Surface presentation and guest touch, keyboard and mouse events |
| Installer/storage | Existing Swift installer/Apple restore integration | Android storage and restore flow, including cancellation and low-space handling |
| Audio/network/sharing | Apple app integrations remain | Android audio, network and lifecycle integration tests |
| Compatibility/performance | No macOS boot or GPU performance evidence | Real device boot, rendered output and sustained-load measurements |

Installing a Turnip driver alone cannot replace Henge's Metal renderer or translate guest Metal shaders. This stage makes CPU host portability testable before those larger components are implemented. Do not interpret upstream iOS performance or screenshots as Android results.

The original GPL-2.0-or-later license and attribution remain in place. No macOS images or Apple proprietary binaries are included.
