#pragma once

#include "managers/Memory.h"

#include <atomic>

namespace SInput
{
    extern uint32_t system_id;
    extern MMemory::LinearAllocator32kb task_args_memory;

    void init_input(uint32_t);

    uint64_t submit_tasks(void*, uint32_t);
    uint64_t input_task(void*, uint32_t);
}
