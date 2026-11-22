#ifndef HENGE_ANDROID_PROBE_H
#define HENGE_ANDROID_PROBE_H
#include <stddef.h>
/* Runs a small, bounded guest program through the actual Henge translator.
   Caller must serialize this with any other Henge machine activity. */
int hv_android_cpu_probe(char *report, size_t capacity);
#endif
