#pragma once

#include <thread>

#define ALIGN(x) __attribute__((aligned(x)))

#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(z, y, x, w) (z<<6) | (y<<4) | (x<<2) | w
#endif

namespace MPlatform
{
    const uint32_t NUM_HARDWARE_THREADS = std::thread::hardware_concurrency();

    inline uint32_t asm_bsf32(uint32_t m)
    {
        uint32_t i;
        __asm__ ("bsfl %1, %0"
                :"=r" (i)
                :"r" (m)
                :"cc");
        return i;
    }

    inline uint32_t asm_bsr32(uint32_t m)
    {
        uint32_t i;
        __asm__ ("bsrl %1, %0"
                :"=r" (i)
                :"r" (m)
                :"cc");
        return i;
    }

    inline uint64_t asm_bsf64(uint64_t m)
    {
        uint64_t i;
        __asm__ ("bsfq %1, %0"
                :"=r" (i)
                :"r" (m)
                :"cc");
        return i;
    }

    inline uint64_t asm_bsr64(uint64_t m)
    {
        uint64_t i;
        __asm__ ("bsrq %1, %0"
                :"=r" (i)
                :"r" (m)
                :"cc");
        return i;
    }

    inline uint64_t asm_rdtscp(void)
    {
        uint64_t tsc;
        __asm__ __volatile__(
            "rdtscp;"
            "shl $32, %%rdx;"
            "or %%rdx, %%rax"
            : "=a"(tsc)
            :
            : "%rcx", "%rdx");
        return tsc;
    }
}
