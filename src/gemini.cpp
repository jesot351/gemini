#include "managers/Memory.h"
#include "managers/TaskScheduling.h"
#include "systems/Input.h"
#include "systems/Physics.h"
#include "systems/Animation.h"
#include "systems/AI.h"
#include "systems/Rendering.h"

#include <iostream>
#include <thread>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

int main()
{
    MMemory::init_memory();
    MTaskScheduling::init_scheduler();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "gemini", nullptr, nullptr);

    SInput::init_input(0);
    SPhysics::init_physics(1);
    SAnimation::init_animation(2);
    SAI::init_ai(3);
    SRendering::init_rendering(4);

    uint32_t num_threads = MTaskScheduling::NUM_WORKER_THREADS;
    std::cout << "num threads: " << num_threads << "\n";

    std::thread workers[MTaskScheduling::MAX_NUM_WORKER_THREADS];
    for (uint32_t i = 0; i < num_threads; ++i)
    {
        workers[i] = std::thread(MTaskScheduling::worker_thread, i);
    }

    for (uint32_t i = 0; i < num_threads; ++i)
    {
        workers[i].join();
    }

#if PROFILING
    MTaskScheduling::write_profiling();
#endif

    std::cout << "total executed: " << MTaskScheduling::g_total_executed.load(std::memory_order_relaxed) << "\n";

    MMemory::clear_memory();

    return 0;
}
