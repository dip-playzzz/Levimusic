#include "Platform.hpp"

extern "C" __attribute__((visibility("hidden")))
void levi_clear_cache(void* begin, void* end) {
#if defined(__aarch64__)
    uptr start = reinterpret_cast<uptr>(begin);
    const uptr finish = reinterpret_cast<uptr>(end);
    u64 ctr = 0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));

    if ((ctr & (1ull << 28)) == 0) {
        const uptr dLine = 4ull << ((ctr >> 16) & 0xF);
        for (uptr p = start & ~(dLine - 1); p < finish; p += dLine) {
            __asm__ volatile("dc cvau, %0" :: "r"(p) : "memory");
        }
        __asm__ volatile("dsb ish" ::: "memory");
    }

    const uptr iLine = 4ull << (ctr & 0xF);
    for (uptr p = start & ~(iLine - 1); p < finish; p += iLine) {
        __asm__ volatile("ic ivau, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish\nisb" ::: "memory");
#else
    (void)begin;
    (void)end;
#endif
}
