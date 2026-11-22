#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "android_probe.h"

static pthread_mutex_t engine_lock = PTHREAD_MUTEX_INITIALIZER;

/* Enumeration only. This is deliberately not a Metal-to-Vulkan renderer. */
static void vulkan_report(char *out, size_t size) {
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        snprintf(out, size, "Vulkan: loader tidak tersedia.");
        return;
    }
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance create = get ? (PFN_vkCreateInstance)get(VK_NULL_HANDLE, "vkCreateInstance") : NULL;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Henge Android diagnostics", .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult status = create ? create(&info, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
    if (status != VK_SUCCESS) {
        snprintf(out, size, "Vulkan: gagal membuat instance (%d).", status);
        dlclose(lib);
        return;
    }
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)get(instance, "vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)get(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties = (PFN_vkGetPhysicalDeviceProperties)get(instance, "vkGetPhysicalDeviceProperties");
    uint32_t count = 0;
    status = enumerate ? enumerate(instance, &count, NULL) : VK_ERROR_INITIALIZATION_FAILED;
    if (status != VK_SUCCESS || !count || !properties) {
        snprintf(out, size, "Vulkan: tidak ada GPU yang dapat dibaca (%d).", status);
    } else {
        /* A small bounded list is enough for a phone/tablet diagnostic. */
        VkPhysicalDevice devices[8];
        if (count > 8) count = 8;
        status = enumerate(instance, &count, devices);
        if ((status == VK_SUCCESS || status == VK_INCOMPLETE) && count) {
            VkPhysicalDeviceProperties p;
            properties(devices[0], &p);
            snprintf(out, size, "Vulkan: tersedia\nGPU: %s\nAPI: %u.%u.%u\nDriver: 0x%x\n"
                     "Informasi ini belum membuktikan renderer macOS berfungsi.",
                     p.deviceName, VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
                     VK_VERSION_PATCH(p.apiVersion), p.driverVersion);
        } else snprintf(out, size, "Vulkan: enumerasi gagal (%d).", status);
    }
    if (destroy) destroy(instance, NULL);
    dlclose(lib);
}

JNIEXPORT jstring JNICALL
Java_io_github_bxiidn_henge_MainActivity_nativeDiagnostics(JNIEnv *env, jclass cls) {
    (void)cls;
    char cpu[1024], gpu[1024], report[2200];
    pthread_mutex_lock(&engine_lock);
    hv_android_cpu_probe(cpu, sizeof cpu);
    pthread_mutex_unlock(&engine_lock);
    vulkan_report(gpu, sizeof gpu);
    snprintf(report, sizeof report, "%s\n\n%s", cpu, gpu);
    return (*env)->NewStringUTF(env, report);
}

#include "headless.h"
static _Atomic int boot_cancel;
static pthread_mutex_t serial_lock = PTHREAD_MUTEX_INITIALIZER;
static char boot_serial[32768];
static size_t boot_serial_used;
static void live_serial(void *unused, uint8_t c) {
    (void)unused;
    pthread_mutex_lock(&serial_lock);
    if (boot_serial_used + 1 < sizeof boot_serial) {
        boot_serial[boot_serial_used++] = (char)c;
        boot_serial[boot_serial_used] = 0;
    }
    pthread_mutex_unlock(&serial_lock);
}
JNIEXPORT void JNICALL Java_io_github_bxiidn_henge_BootActivity_nativePrepare(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    atomic_store(&boot_cancel, 0);
    pthread_mutex_lock(&serial_lock);
    boot_serial[0] = 0; boot_serial_used = 0;
    pthread_mutex_unlock(&serial_lock);
}
JNIEXPORT void JNICALL Java_io_github_bxiidn_henge_BootActivity_nativeStop(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; atomic_store(&boot_cancel, 1);
}
JNIEXPORT jstring JNICALL Java_io_github_bxiidn_henge_BootActivity_nativeSerial(JNIEnv *env, jclass cls) {
    (void)cls;
    pthread_mutex_lock(&serial_lock);
    jstring text = (*env)->NewStringUTF(env, boot_serial);
    pthread_mutex_unlock(&serial_lock);
    return text;
}
JNIEXPORT jstring JNICALL Java_io_github_bxiidn_henge_BootActivity_nativeBoot(
    JNIEnv *env, jclass cls, jint rom, jint disk, jint aux, jint ram,
    jlong ecid, jlong seed, jboolean selftest) {
    (void)cls;
    HvHeadlessConfig cfg = {.rom_fd=rom, .disk_fd=disk, .aux_fd=aux,
        .ram_mb=(unsigned)ram, .timeout_ms=selftest ? 5000 : 30000,
        .ecid=(uint64_t)ecid, .seed=(uint64_t)seed, .aux_offset=16384,
        .selftest=selftest, .serial_output=live_serial};
    char *report = malloc(40000);
    if (!report) return (*env)->NewStringUTF(env, "Memori laporan tidak cukup.");
    pthread_mutex_lock(&engine_lock);
    hv_headless_run(&cfg, &boot_cancel, report, 40000);
    pthread_mutex_unlock(&engine_lock);
    jstring result = (*env)->NewStringUTF(env, report);
    free(report);
    return result;
}
