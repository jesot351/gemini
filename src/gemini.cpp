#include "gemini.h"
#include "managers/Memory.h"
#include "managers/TaskScheduling.h"
#include "systems/input/Input.h"
#include "systems/physics/Physics.h"
#include "systems/animation/Animation.h"
#include "systems/ai/AI.h"
#include "systems/rendering/Rendering.h"

#include <iostream>
#include <thread>
#include <mutex>

#include <GLFW/glfw3.h>

int main()
{
    // Determine number of worker threads
    uint32_t num_threads;
    std::cout << "Number of worker threads: ";
    std::cin >> num_threads;
    MTaskScheduling::NUM_WORKER_THREADS = num_threads;

    // Initialize managers
    MMemory::init_memory();
    MTaskScheduling::init_scheduler();

    // Initialize window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "gemini", nullptr, nullptr);

    // Initialize systems
    SInput::init_input(0);
    SPhysics::init_physics(1);
    SAnimation::init_animation(2);
    SAI::init_ai(3);
    SRendering::init_rendering(4, window);

    // Launch worker threads
    std::thread workers[MTaskScheduling::MAX_NUM_WORKER_THREADS];
    for (uint32_t i = 0; i < num_threads; ++i)
    {
        workers[i] = std::thread(MTaskScheduling::worker_thread, i);
    }

    // Enter input event loop
    SInput::input_loop(window);

    // Shut down worker threads
    for (uint32_t i = 0; i < num_threads; ++i)
    {
        workers[i].join();
    }

    MTaskScheduling::write_profiling();

    std::cout << "total executed: " << MTaskScheduling::g_total_executed.load(std::memory_order_relaxed) << "\n";

    // Clear resources
    SRendering::clear_rendering();
    MMemory::clear_memory();

    // Destroy window
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

void signal_shutdown()
{
    // signal task scheduling manager
    MTaskScheduling::g_quit_request.store(1, std::memory_order_relaxed);

    // signal input handling
    {
        std::unique_lock<std::mutex> l(SInput::input_loop_sync.m);
        SInput::input_loop_sync.gather_input = true;
        SInput::input_loop_sync.input_gathered = true;
    }
    SInput::input_loop_sync.cv.notify_one();
}
