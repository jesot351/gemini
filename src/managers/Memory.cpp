#include "managers/Memory.h"
#include "managers/Platform.h"

#include <cstdlib>
#include <cassert>
#include <iostream>

//template <typename allocator>
void* operator new(size_t size, MMemory::LinearAllocator32kb& alloc, size_t count, uint8_t alignment)
{
    return alloc.Allocate(size * count, alignment);
}

namespace MMemory
{
    void* _mem512x32kb;
    ALIGN(64) std::atomic<uint64_t> _mem512x32kb_allocmask[8];

    void init_memory()
    {
        _mem512x32kb = std::malloc(512*32*1024);
        assert(_mem512x32kb);
        for (int i = 0; i < 8; ++i)
        {
            _mem512x32kb_allocmask[i].store(0xFFFFFFFFFFFFFFFF, std::memory_order_relaxed);
        }
    }

    void clear_memory()
    {
        std::free(_mem512x32kb);
    }

    void LinearAllocator32kb::Init()
    {
        uint32_t segment = 0;
        uint32_t block;
        uint64_t old;
        do
        {
            old = _mem512x32kb_allocmask[segment];
            block = MPlatform::asm_bsf64(old);

            if (!old)
            {
                ++segment;  // what to do if full?
                assert(segment < 9);
            }
        } while ( !(old && _mem512x32kb_allocmask[segment].compare_exchange_weak(old, old & ~((uint64_t)1<<block), std::memory_order_relaxed)) );

        m_position = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(_mem512x32kb) + segment*64*32*1024 + block*32*1024);
        m_segment = segment;
        m_block = block;
    }

    LinearAllocator32kb::~LinearAllocator32kb()
    {
        _mem512x32kb_allocmask[m_segment].fetch_or((uint64_t)1<<m_block, std::memory_order_relaxed);
        m_position = nullptr;
    }

    void LinearAllocator32kb::Clear()
    {
        m_position = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(_mem512x32kb) + m_segment*64*32*1024 + m_block*32*1024);
    }

    void* LinearAllocator32kb::Allocate(size_t size, uint8_t alignment)
    {
        // what to do if full?
        assert(reinterpret_cast<uintptr_t>(m_position) + size + alignment - (reinterpret_cast<uintptr_t>(_mem512x32kb) + m_segment*64*32*1024 + m_block*32*1024) < 32*1024);
        void* ret = reinterpret_cast<void*>( ( reinterpret_cast<uintptr_t>(m_position) + alignment - 1 ) & ~(alignment-1) );
        m_position = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ret) + size);

        return ret;
    }

    void ConcurrentLinearAllocator32kb::Init()
    {
        uint32_t segment = 0;
        uint32_t block;
        uint64_t old;
        do
        {
            old = _mem512x32kb_allocmask[segment];
            block = MPlatform::asm_bsf64(old);

            if (!old)
            {
                ++segment;  // what to do if full?
                assert(segment < 9);
            }
        } while ( !(old && _mem512x32kb_allocmask[segment].compare_exchange_weak(old, old & ~((uint64_t)1<<block), std::memory_order_relaxed)) );

        m_position.store(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(_mem512x32kb) + segment*64*32*1024 + block*32*1024), std::memory_order_relaxed);
        m_segment = segment;
        m_block = block;
    }

    ConcurrentLinearAllocator32kb::~ConcurrentLinearAllocator32kb()
    {
        _mem512x32kb_allocmask[m_segment].fetch_or((uint64_t)1<<m_block, std::memory_order_relaxed);
        m_position.store(nullptr, std::memory_order_relaxed);
    }

    void ConcurrentLinearAllocator32kb::Clear()
    {
        m_position.store(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(_mem512x32kb) + m_segment*64*32*1024 + m_block*32*1024), std::memory_order_relaxed);
    }

    void* ConcurrentLinearAllocator32kb::Allocate(size_t size, uint8_t alignment)
    {
        void* old = m_position.load(std::memory_order_relaxed);
        void* ret;
        do
        {
            // what to do if full?
            assert(reinterpret_cast<uintptr_t>(old) + size + alignment - (reinterpret_cast<uintptr_t>(_mem512x32kb) + m_segment*64*32*1024 + m_block*32*1024) < 32*1024);
            ret = reinterpret_cast<void*>( ( reinterpret_cast<uintptr_t>(old) + alignment - 1 ) & ~(alignment-1) );
        } while ( !m_position.compare_exchange_weak(old, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ret) + size), std::memory_order_relaxed) );

        return ret;
    }
}
