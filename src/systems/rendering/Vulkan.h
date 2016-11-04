#pragma once

#include <stdint.h>
#include <vector>
#include <array>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct GLFWwindow;

namespace SRendering
{
    typedef struct vertex_t {
        glm::vec2 position;
        glm::vec3 color;

        static VkVertexInputBindingDescription get_binding_description()
        {
            VkVertexInputBindingDescription binding_description = {};
            binding_description.binding = 0;
            binding_description.stride = sizeof(vertex_t);
            binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return binding_description;
        }

        static std::array<VkVertexInputAttributeDescription, 2> get_attribute_descriptions()
        {
            std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions = {};
            attribute_descriptions[0].binding = 0;
            attribute_descriptions[0].location = 0;
            attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
            attribute_descriptions[0].offset = offsetof(vertex_t, position);
            attribute_descriptions[1].binding = 0;
            attribute_descriptions[1].location = 1;
            attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attribute_descriptions[1].offset = offsetof(vertex_t, color);

            return attribute_descriptions;
        }
    } vertex_t;

    typedef struct queue_family_indices_t
    {
        int32_t graphics = -1;
        int32_t present = -1;

        bool is_complete()
        {
            return graphics >= 0 && present >= 0;
        }
    } queue_family_indices_t;

    typedef struct swapchain_support_details_t
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> present_modes;
    } swapchain_support_details_t;

    void init_vulkan(GLFWwindow*);
    void clear_vulkan();

    void create_instance();

    void setup_debug_callback();
    VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugReportFlagsEXT,
        VkDebugReportObjectTypeEXT,
        uint64_t,
        size_t,
        int32_t,
        const char*,
        const char*,
        void*);

    void create_surface(GLFWwindow*);

    void pick_physical_device();
    bool is_device_suitable(VkPhysicalDevice);
    queue_family_indices_t find_queue_families(VkPhysicalDevice);
    bool check_device_extension_support(VkPhysicalDevice);

    void create_logical_device();

    void create_swapchain();
    swapchain_support_details_t query_swapchain_support(VkPhysicalDevice);
    VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>*);
    VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>*);
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR*);
    void create_image_views();
    void create_framebuffers();

    void create_render_pass();
    void create_graphics_pipeline();
    void create_shader_module_from_file(const char*, VkShaderModule*);

    void create_command_pool();
    void create_command_buffers();

    void create_buffer(VkDeviceSize, VkBufferUsageFlags,
                       VkMemoryPropertyFlags, VkBuffer*, VkDeviceMemory*);
    void copy_buffer(VkBuffer, VkBuffer, VkDeviceSize);
    void create_vertex_buffer();
    void create_index_buffer();
    uint32_t find_memory_type(uint32_t, VkMemoryPropertyFlags);

    void create_semaphores();

    void draw_frame();
}
