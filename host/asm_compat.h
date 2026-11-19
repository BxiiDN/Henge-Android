#ifndef HENGE_ASM_COMPAT_H
#define HENGE_ASM_COMPAT_H
#ifdef __APPLE__
#define HV_SYMBOL(n) _##n
#define HV_PAGE(n) n@PAGE
#define HV_PAGEOFF(n) n@PAGEOFF
#else
#define HV_SYMBOL(n) n
#define HV_PAGE(n) n
#define HV_PAGEOFF(n) :lo12:n
#endif
#endif
