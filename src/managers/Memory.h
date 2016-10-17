#pragma once

#include "managers/Platform.h"

#include <atomic>


namespace MMemory
{
    extern void* _mem512x32kb;
    extern ALIGN(64) std::atomic<uint64_t> _mem512x32kb_allocmask[8];

    void init_memory();
    void clear_memory();

    struct LinearAllocator32kb
    {
        LinearAllocator32kb() = default;
        LinearAllocator32kb(const LinearAllocator32kb&) = delete;
        LinearAllocator32kb(LinearAllocator32kb&&) = delete;
        LinearAllocator32kb& operator=(const LinearAllocator32kb&) = delete;
        LinearAllocator32kb& operator=(LinearAllocator32kb&&) = delete;
        ~LinearAllocator32kb();

        void* Allocate(size_t size, uint8_t alignment = 4);
        void Init();
        void Clear();

        void* m_position;
        uint32_t m_segment;
        uint32_t m_block;
    };

    struct ConcurrentLinearAllocator32kb
    {
        ConcurrentLinearAllocator32kb() = default;
        ConcurrentLinearAllocator32kb(const ConcurrentLinearAllocator32kb&) = delete;
        ConcurrentLinearAllocator32kb(ConcurrentLinearAllocator32kb&&) = delete;
        ConcurrentLinearAllocator32kb& operator=(const ConcurrentLinearAllocator32kb&) = delete;
        ConcurrentLinearAllocator32kb& operator=(ConcurrentLinearAllocator32kb&&) = delete;
        ~ConcurrentLinearAllocator32kb();

        void* Allocate(size_t size, uint8_t alignment = 4);
        void Init();
        void Clear();

        std::atomic<void*> m_position;
        uint32_t m_segment;
        uint32_t m_block;
    };
}
//template <typename allocator>
void* operator new(size_t size, MMemory::LinearAllocator32kb& alloc, size_t count = 1, uint8_t alignment = 4);
