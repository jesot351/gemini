#include "gemini.h"
#include "systems/rendering/Vulkan.h"

#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <limits>
#include <cstring>
#include <chrono>
#include <time.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace SRendering
{
    VkInstance instance;
    VkDebugReportCallbackEXT callback;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
    VkDevice device;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;

    struct {
        VkImage images[4];
        VkDeviceMemory image_memory[4];
        VkImageView image_views[4];
        VkFramebuffer framebuffer;
        VkSampler sampler;
    } gbuffer;

    VkCommandPool graphics_command_pool;
    VkCommandPool compute_command_pool;

    VkDeviceMemory vertex_buffer_memory;
    VkBuffer vertex_buffer;
    VkDeviceMemory index_buffer_memory;
    VkBuffer index_buffer;

    VkQueue graphics_queue;
    VkQueue compute_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_index;
    uint32_t compute_queue_index;
    uint32_t present_queue_index;

    VkRenderPass render_pass;

    VkDeviceMemory graphics_uniform_buffer_memory;
    VkBuffer graphics_uniform_buffer;
    VkPipelineLayout graphics_pipeline_layout;
    VkDescriptorSetLayout graphics_descriptor_set_layout;
    VkPipeline graphics_pipeline;
    VkDescriptorPool graphics_descriptor_pool;
    VkDescriptorSet graphics_descriptor_set;

    VkDeviceMemory compute_uniform_buffer_memory;
    VkBuffer compute_uniform_buffer;
    VkPipelineLayout compute_pipeline_layout;
    VkDescriptorSetLayout compute_input_descriptor_set_layout;
    VkDescriptorSetLayout compute_output_descriptor_set_layout;
    VkPipeline compute_pipeline;
    VkDescriptorPool compute_descriptor_pool;
    VkDescriptorSet compute_input_descriptor_set;
    std::vector<VkDescriptorSet> compute_output_descriptor_sets;

    VkCommandBuffer graphics_command_buffer;
    std::vector<VkCommandBuffer> compute_command_buffers;

    VkSemaphore image_available_semaphore;
    VkSemaphore gbuffer_finished_semaphore;
    VkSemaphore compute_finished_semaphore;

    VkFence compute_finished_fence;

    struct {
        const uint32_t num_lights = 32;
        VkBuffer storage_buffer;
        VkDeviceMemory storage_buffer_memory;
        light_t* data;
        float* animation_offsets;
        const float animation_period = 3.1415926535f; // up and down in 2 sec
    } lights;

    const char* validation_layers[] = {
        "VK_LAYER_LUNARG_standard_validation"
    };

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    const uint32_t num_plane_vertices = 4;
    const vertex_t plane_vertices[num_plane_vertices] = {
        {{-2.0f, 0.0f, -2.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{2.0f, 0.0f, -2.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{2.0f, 0.0f, 2.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-2.0f, 0.0f, 2.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}
    };

    const uint32_t num_plane_indices = 6;
    const uint32_t plane_indices[num_plane_indices] = {
        0, 1, 2, 2, 3, 0,
    };

    void init_vulkan(GLFWwindow* window)
    {
        create_instance();
        setup_debug_callback();
        create_surface(window);
        pick_physical_device();
        create_logical_device();
        create_swapchain();

        create_graphics_command_pool();
        create_compute_command_pool();

        create_render_pass();
        create_gbuffer();

        create_vertex_buffer();
        create_index_buffer();
        create_uniform_buffers();

        init_lights();

        create_graphics_descriptor_set_layouts();
        create_graphics_pipeline();
        create_graphics_descriptor_pool();
        create_graphics_descriptor_sets();

        create_compute_descriptor_set_layouts();
        create_compute_pipeline();
        create_compute_descriptor_pool();
        create_compute_descriptor_sets();

        create_graphics_command_buffers();
        create_compute_command_buffers();

        create_sync_primitives();
    }

    void create_instance()
    {
        VkApplicationInfo app_info = {};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "gemini";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "gemini";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        std::vector<const char*> extensions;
        uint32_t num_glfw_extensions = 0;
        const char** glfw_extensions;
        glfw_extensions = glfwGetRequiredInstanceExtensions(&num_glfw_extensions);
        for (uint32_t i = 0; i < num_glfw_extensions; ++i)
        {
            extensions.push_back(glfw_extensions[i]);
        }
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

        VkInstanceCreateInfo instance_info = {};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;
        instance_info.enabledExtensionCount = extensions.size();
        instance_info.ppEnabledExtensionNames = extensions.data();
        instance_info.enabledLayerCount = 1;
        instance_info.ppEnabledLayerNames = validation_layers;

        VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
        assert(result == VK_SUCCESS);
    }

    void setup_debug_callback()
    {
        VkDebugReportCallbackCreateInfoEXT callback_info = {};
        callback_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        callback_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        callback_info.pfnCallback = debug_callback;

        auto fn = (PFN_vkCreateDebugReportCallbackEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
        assert(fn);
        VkResult result = fn(instance, &callback_info, nullptr, &callback);
        assert(result == VK_SUCCESS);
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT obj_type,
        uint64_t obj,
        size_t location,
        int32_t code,
        const char* layer_prefix,
        const char* msg,
        void* user_data)
    {
        std::cerr << "validation layer: " << msg << std::endl;

        return VK_FALSE;
    }

    void create_surface(GLFWwindow* window)
    {
        VkResult result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
        assert(result == VK_SUCCESS);
    }

    void pick_physical_device()
    {
        uint32_t num_devices = 0;
        vkEnumeratePhysicalDevices(instance, &num_devices, nullptr);
        std::cout << "found " << num_devices << " physical devices." << std::endl;
        assert(num_devices != 0);
        std::vector<VkPhysicalDevice> devices(num_devices);
        vkEnumeratePhysicalDevices(instance, &num_devices, devices.data());

        for (const VkPhysicalDevice& device : devices)
        {
            uint32_t graphics, compute, present;
            bool has_all_queues = find_queue_family_indices(device, &graphics, &compute, &present);
            bool supports_all_extensions = check_device_extension_support(device);
            bool has_adequate_swapchain = false;
            if (supports_all_extensions)
            {
                swapchain_support_details_t details = query_swapchain_support(device);
                has_adequate_swapchain = !details.formats.empty() && !details.present_modes.empty();
            }

            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(device, &device_properties);
            bool is_dedicated_gpu = device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

            if (has_all_queues && supports_all_extensions && has_adequate_swapchain && is_dedicated_gpu)
            {
                physical_device = device;
                graphics_queue_index = graphics;
                compute_queue_index = compute;
                present_queue_index = present;

                break;
            }
        }

        vkGetPhysicalDeviceMemoryProperties(physical_device, &physical_device_memory_properties);

        assert(physical_device != VK_NULL_HANDLE);
    }

    bool find_queue_family_indices(VkPhysicalDevice physical_device,
                                   uint32_t* graphics_index,
                                   uint32_t* compute_index,
                                   uint32_t* present_index)
    {
        uint32_t num_queue_families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_queue_families, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(num_queue_families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_queue_families, queue_families.data());

        int32_t graphics = -1;
        int32_t compute = -1;
        int32_t present = -1;
        for (uint32_t i = 0; i < queue_families.size(); ++i)
        {
            if (queue_families[i].queueCount > 0 &&
                queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphics = i;
            }

            if (queue_families[i].queueCount > 0 &&
                queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                compute = i;
            }

            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support);

            if (queue_families[i].queueCount > 0 && present_support)
            {
                present = i;
            }

            if (graphics >= 0 && compute >= 0 && present >= 0)
            {
                *graphics_index = (uint32_t) graphics;
                *compute_index = (uint32_t) compute;
                *present_index = (uint32_t) present;

                return true;
            }
        }

        return false;
    }

    bool check_device_extension_support(VkPhysicalDevice physical_device)
    {
        uint32_t num_extensions = 0;
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &num_extensions, nullptr);

        std::vector<VkExtensionProperties> available_extensions(num_extensions);
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &num_extensions, available_extensions.data());

        std::set<std::string> required_extensions(device_extensions.begin(), device_extensions.end());

        for (const VkExtensionProperties& extension : available_extensions)
        {
            required_extensions.erase(extension.extensionName);
        }

        return required_extensions.empty();
    }

    void create_logical_device()
    {
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        std::set<uint32_t> unique_queue_families = {
            graphics_queue_index, compute_queue_index, present_queue_index
        };
        float queue_priority = 1.0f;

        for (uint32_t queue_family : unique_queue_families)
        {
            VkDeviceQueueCreateInfo queue_info = {};
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info.queueFamilyIndex = queue_family;
            queue_info.queueCount = 1;
            queue_info.pQueuePriorities = &queue_priority;
            queue_infos.push_back(queue_info);
        }

        VkPhysicalDeviceFeatures device_features = {};

        VkDeviceCreateInfo device_info = {};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = (uint32_t) queue_infos.size();
        device_info.pQueueCreateInfos = queue_infos.data();
        device_info.pEnabledFeatures = &device_features;
        device_info.enabledExtensionCount = device_extensions.size();
        device_info.ppEnabledExtensionNames = device_extensions.data();
        device_info.enabledLayerCount = 1;
        device_info.ppEnabledLayerNames = validation_layers;

        VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
        assert(result == VK_SUCCESS);

        vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);
        vkGetDeviceQueue(device, compute_queue_index, 0, &compute_queue);
        vkGetDeviceQueue(device, present_queue_index, 0, &present_queue);
    }

    void create_swapchain()
    {
        swapchain_support_details_t details = query_swapchain_support(physical_device);

        VkSurfaceFormatKHR surface_format = choose_swap_surface_format(&details.formats);

        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, surface_format.format, &props);
        std::cout << "chose swap format " << surface_format.format
                  << " with optimal tiling " << props.optimalTilingFeatures << std::endl;

        VkPresentModeKHR present_mode = choose_swap_present_mode(&details.present_modes);
        VkExtent2D extent = choose_swap_extent(&details.capabilities);

        uint32_t num_images = details.capabilities.minImageCount + 1;
        if (details.capabilities.minImageCount > 0 &&
            num_images > details.capabilities.maxImageCount)
        {
            num_images = details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR sc_info = {};
        sc_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sc_info.surface = surface;
        sc_info.minImageCount = num_images;
        sc_info.imageFormat = surface_format.format;
        sc_info.imageColorSpace = surface_format.colorSpace;
        sc_info.imageExtent = extent;
        sc_info.imageArrayLayers = 1;
        sc_info.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT;

        uint32_t queue_family_indices[] = {
            compute_queue_index,
            present_queue_index
        };

        if (compute_queue_index != present_queue_index)
        {
            sc_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            sc_info.queueFamilyIndexCount = 2;
            sc_info.pQueueFamilyIndices = queue_family_indices;
        }
        else
        {
            sc_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            sc_info.queueFamilyIndexCount = 0;
            sc_info.pQueueFamilyIndices = nullptr;
        }

        sc_info.preTransform = details.capabilities.currentTransform;
        sc_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sc_info.presentMode = present_mode;
        sc_info.clipped = VK_TRUE;
        sc_info.oldSwapchain = VK_NULL_HANDLE;

        VkResult result = vkCreateSwapchainKHR(device, &sc_info, nullptr, &swapchain);
        assert(result == VK_SUCCESS);

        vkGetSwapchainImagesKHR(device, swapchain, &num_images, nullptr);
        swapchain_images.resize(num_images);
        vkGetSwapchainImagesKHR(device, swapchain, &num_images, swapchain_images.data());

        swapchain_image_format = surface_format.format;
        swapchain_extent = extent;

        swapchain_image_views.resize(swapchain_images.size());

        for (uint32_t i = 0; i < swapchain_images.size(); ++i)
        {
            VkImageViewCreateInfo view_info = {};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = swapchain_images[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_image_format;
            view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            VkResult result = vkCreateImageView(device, &view_info, nullptr, &swapchain_image_views[i]);
            assert(result == VK_SUCCESS);
        }
    }

    swapchain_support_details_t query_swapchain_support(VkPhysicalDevice physical_device)
    {
        swapchain_support_details_t details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &details.capabilities);

        uint32_t num_formats = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &num_formats, nullptr);
        std::cout << "found " << num_formats << " surface formats." << std::endl;

        if (num_formats != 0)
        {
            details.formats.resize(num_formats);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &num_formats, details.formats.data());
        }

        uint32_t num_present_modes = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &num_present_modes, nullptr);
        std::cout << "found " << num_present_modes << " present modes." << std::endl;

        if (num_present_modes != 0)
        {
            details.present_modes.resize(num_present_modes);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &num_present_modes, details.present_modes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>* available_formats)
    {
        if (available_formats->size() == 1 && (*available_formats)[0].format == VK_FORMAT_UNDEFINED)
        {
            return {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }

        for (const VkSurfaceFormatKHR& available_format : *available_formats)
        {
            std::cout << "available swap format: " << available_format.format << std::endl;
            if (available_format.format == VK_FORMAT_R8G8B8A8_UNORM &&
                available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return available_format;
            }
        }

        return (*available_formats)[0];
    }

    VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>* available_present_modes)
    {

        for (const VkPresentModeKHR& available_present_mode : *available_present_modes)
        {
            if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return available_present_mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR* capabilities)
    {
        if (capabilities->currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities->currentExtent;
        }
        else
        {
            VkExtent2D actual_extent;
            actual_extent.width = std::max(capabilities->minImageExtent.width,
                                           std::min(capabilities->maxImageExtent.width, WIDTH));
            actual_extent.height = std::max(capabilities->minImageExtent.height,
                                           std::min(capabilities->maxImageExtent.height, HEIGHT));

            return actual_extent;
        }
    }

    void create_render_pass()
    {
        VkAttachmentDescription attachments[4] = {};
        for (uint32_t i = 0; i < 3; ++i)
        {
            attachments[i].format = VK_FORMAT_R16G16B16A16_SFLOAT;
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        attachments[3].format = VK_FORMAT_D32_SFLOAT;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_attachment_refs[3] = {};
        for (uint32_t i = 0; i < 3; ++i)
        {
            color_attachment_refs[i].attachment = i;
            color_attachment_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference depth_attachment_ref = {};
        depth_attachment_ref.attachment = 3;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 3;
        subpass.pColorAttachments = color_attachment_refs;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

        VkSubpassDependency dependencies[2] = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 4;
        render_pass_info.pAttachments = attachments;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 2;
        render_pass_info.pDependencies = dependencies;

        VkResult result = vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass);
        assert(result == VK_SUCCESS);
    }

    void create_gbuffer()
    {
        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = swapchain_extent.width;
        image_info.extent.height = swapchain_extent.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.flags = 0;

        uint32_t queue_family_indices[] = {
            graphics_queue_index,
            compute_queue_index
        };

        if (graphics_queue_index != compute_queue_index)
        {
            image_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            image_info.queueFamilyIndexCount = 2;
            image_info.pQueueFamilyIndices = queue_family_indices;
        }
        else
        {
            image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            image_info.queueFamilyIndexCount = 0;
            image_info.pQueueFamilyIndices = nullptr;
        }

        VkResult result;
        for (uint32_t i = 0; i < 3; ++i)
        {
            result = vkCreateImage(device, &image_info, nullptr, &gbuffer.images[i]);
            assert(result == VK_SUCCESS);
        }

        image_info.format = VK_FORMAT_D32_SFLOAT;
        image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        result = vkCreateImage(device, &image_info, nullptr, &gbuffer.images[3]);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

        for (uint32_t i = 0; i < 4; ++i)
        {
            VkMemoryRequirements memory_requirements;
            vkGetImageMemoryRequirements(device, gbuffer.images[i], &memory_requirements);

            alloc_info.allocationSize = memory_requirements.size;
            alloc_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            result = vkAllocateMemory(device, &alloc_info, nullptr, &gbuffer.image_memory[i]);
            assert(result == VK_SUCCESS);
            result = vkBindImageMemory(device, gbuffer.images[i], gbuffer.image_memory[i], 0);
            assert(result == VK_SUCCESS);
        }

        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        for (uint32_t i = 0; i < 3; ++i)
        {
            view_info.image = gbuffer.images[i];
            result = vkCreateImageView(device, &view_info, nullptr, &gbuffer.image_views[i]);
            assert(result == VK_SUCCESS);
        }

        view_info.image = gbuffer.images[3];
        view_info.format = VK_FORMAT_D32_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        result = vkCreateImageView(device, &view_info, nullptr, &gbuffer.image_views[3]);

        VkImageView attachments[] = {
            gbuffer.image_views[0],
            gbuffer.image_views[1],
            gbuffer.image_views[2],
            gbuffer.image_views[3]
        };

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = 4;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = swapchain_extent.width;
        framebuffer_info.height = swapchain_extent.height;
        framebuffer_info.layers = 1;

        result = vkCreateFramebuffer(device, &framebuffer_info, nullptr, &gbuffer.framebuffer);
        assert(result == VK_SUCCESS);

        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.maxAnisotropy = 0;
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_TRUE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;

        result = vkCreateSampler(device, &sampler_info, nullptr, &gbuffer.sampler);
        assert(result == VK_SUCCESS);
    }

    void create_graphics_descriptor_set_layouts()
    {
        VkDescriptorSetLayoutBinding ubo_binding = {};
        ubo_binding.binding = 0;
        ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_binding.descriptorCount = 1;
        ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        ubo_binding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &ubo_binding;

        VkResult result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &graphics_descriptor_set_layout);
        assert(result == VK_SUCCESS);
    }

    void create_graphics_pipeline()
    {
        // shader stages
        VkShaderModule vertex_shader_module;
        VkShaderModule fragment_shader_module;
        create_shader_module_from_file("res/shaders/vert.spv", &vertex_shader_module);
        create_shader_module_from_file("res/shaders/frag.spv", &fragment_shader_module);

        VkPipelineShaderStageCreateInfo v_shader_info;
        v_shader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        v_shader_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        v_shader_info.module = vertex_shader_module;
        v_shader_info.pName = "main";

        VkPipelineShaderStageCreateInfo p_shader_info;
        p_shader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        p_shader_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        p_shader_info.module = fragment_shader_module;
        p_shader_info.pName = "main";

        VkPipelineShaderStageCreateInfo shader_infos[] = {
            v_shader_info,
            p_shader_info
        };

        // fixed-function stages
        VkVertexInputBindingDescription binding_description = vertex_t::get_binding_description();
        std::array<VkVertexInputAttributeDescription, 3> attribute_descriptions = vertex_t::get_attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.pVertexBindingDescriptions = &binding_description;
        vertex_input_info.vertexAttributeDescriptionCount = attribute_descriptions.size();
        vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

        VkPipelineInputAssemblyStateCreateInfo input_assemby_info = {};
        input_assemby_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assemby_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assemby_info.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float) swapchain_extent.width;
        viewport.height = (float) swapchain_extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = swapchain_extent;

        VkPipelineViewportStateCreateInfo viewport_info = {};
        viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_info.viewportCount = 1;
        viewport_info.pViewports = &viewport;
        viewport_info.scissorCount = 1;
        viewport_info.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer_info = {};
        rasterizer_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer_info.depthClampEnable = VK_FALSE;
        rasterizer_info.rasterizerDiscardEnable = VK_FALSE;
        rasterizer_info.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer_info.lineWidth = 1.0f;
        rasterizer_info.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer_info.depthBiasEnable = VK_FALSE;
        rasterizer_info.depthBiasConstantFactor = 0.0f;
        rasterizer_info.depthBiasClamp = 0.0f;
        rasterizer_info.depthBiasSlopeFactor = 0.0f;

        VkPipelineMultisampleStateCreateInfo multisampling_info = {};
        multisampling_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling_info.sampleShadingEnable = VK_FALSE;
        multisampling_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling_info.minSampleShading = 1.0;
        multisampling_info.pSampleMask = nullptr;
        multisampling_info.alphaToCoverageEnable = VK_FALSE;
        multisampling_info.alphaToOneEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {};
        depth_stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_info.depthTestEnable = VK_TRUE;
        depth_stencil_info.depthWriteEnable = VK_TRUE;
        depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_info.depthBoundsTestEnable = VK_FALSE;
        depth_stencil_info.minDepthBounds = 0.0f;
        depth_stencil_info.maxDepthBounds = 1.0f;
        depth_stencil_info.stencilTestEnable = VK_FALSE;
        depth_stencil_info.front = {};
        depth_stencil_info.back = {};

        VkPipelineColorBlendAttachmentState blend_attachment_infos[3] = {};
        for (uint32_t i = 0; i < 3; ++i)
        {
            blend_attachment_infos[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            blend_attachment_infos[i].blendEnable = VK_FALSE;
            blend_attachment_infos[i].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment_infos[i].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend_attachment_infos[i].colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment_infos[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment_infos[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend_attachment_infos[i].alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo blend_info = {};
        blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend_info.logicOpEnable = VK_FALSE;
        blend_info.logicOp = VK_LOGIC_OP_COPY;
        blend_info.attachmentCount = 3;
        blend_info.pAttachments = blend_attachment_infos;
        blend_info.blendConstants[0] = 0.0f;
        blend_info.blendConstants[1] = 0.0f;
        blend_info.blendConstants[2] = 0.0f;
        blend_info.blendConstants[3] = 0.0f;

        VkDescriptorSetLayout set_layouts[] = { graphics_descriptor_set_layout };
        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 0;
        layout_info.pPushConstantRanges = nullptr;

        VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &graphics_pipeline_layout);
        assert(result == VK_SUCCESS);

        VkGraphicsPipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_infos;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assemby_info;
        pipeline_info.pViewportState = &viewport_info;
        pipeline_info.pRasterizationState = &rasterizer_info;
        pipeline_info.pMultisampleState = &multisampling_info;
        pipeline_info.pDepthStencilState = &depth_stencil_info;
        pipeline_info.pColorBlendState = &blend_info;
        pipeline_info.pDynamicState = nullptr;
        pipeline_info.layout = graphics_pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_info.basePipelineIndex = -1;

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline);
        assert(result == VK_SUCCESS);

        vkDestroyShaderModule(device, vertex_shader_module, nullptr);
        vkDestroyShaderModule(device, fragment_shader_module, nullptr);
    }

    void create_graphics_descriptor_pool()
    {
        VkDescriptorPoolSize pool_size = {};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;

        VkResult result = vkCreateDescriptorPool(device, &pool_info, nullptr, &graphics_descriptor_pool);
        assert(result == VK_SUCCESS);
    }

    void create_graphics_descriptor_sets()
    {
        VkDescriptorSetLayout layouts[] = { graphics_descriptor_set_layout };
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = graphics_descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = layouts;

        VkResult result = vkAllocateDescriptorSets(device, &alloc_info, &graphics_descriptor_set);
        assert(result == VK_SUCCESS);

        VkDescriptorBufferInfo buffer_info = {};
        buffer_info.buffer = graphics_uniform_buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(ubo_transforms_t);

        VkWriteDescriptorSet descriptor_write = {};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = graphics_descriptor_set;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;
        descriptor_write.pImageInfo = nullptr;
        descriptor_write.pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);
    }

    void create_compute_descriptor_set_layouts()
    {
        VkDescriptorSetLayoutBinding input_bindings[6] = {};

        for (uint32_t i = 0; i < 4; ++i)
        {
            input_bindings[i].binding = i;
            input_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_bindings[i].descriptorCount = 1;
            input_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        input_bindings[4].binding = 4;
        input_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        input_bindings[4].descriptorCount = 1;
        input_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        input_bindings[5].binding = 5;
        input_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        input_bindings[5].descriptorCount = 1;
        input_bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 6;
        layout_info.pBindings = input_bindings;

        VkResult result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &compute_input_descriptor_set_layout);

        VkDescriptorSetLayoutBinding output_binding = {};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        layout_info.bindingCount = 1;
        layout_info.pBindings = &output_binding;

        result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &compute_output_descriptor_set_layout);
        assert(result == VK_SUCCESS);
    }

    void create_compute_pipeline()
    {
        VkShaderModule compute_shader_module;
        create_shader_module_from_file("res/shaders/comp.spv", &compute_shader_module);

        VkPipelineShaderStageCreateInfo shader_info = {};
        shader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shader_info.module = compute_shader_module;
        shader_info.pName = "main";

        VkDescriptorSetLayout set_layouts[] = {
            compute_input_descriptor_set_layout,
            compute_output_descriptor_set_layout
        };
        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 0;
        layout_info.pPushConstantRanges = nullptr;

        VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &compute_pipeline_layout);
        assert(result == VK_SUCCESS);

        VkComputePipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = shader_info;
        pipeline_info.layout = compute_pipeline_layout;

        result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &compute_pipeline);
        assert(result == VK_SUCCESS);

        vkDestroyShaderModule(device, compute_shader_module, nullptr);
    }

    void create_compute_descriptor_pool()
    {
        VkDescriptorPoolSize pool_sizes[4] = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = 4;

        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[1].descriptorCount = (uint32_t) swapchain_images.size();

        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[2].descriptorCount = 1;

        pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[3].descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 4;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = swapchain_images.size() + 1;

        VkResult result = vkCreateDescriptorPool(device, &pool_info, nullptr, &compute_descriptor_pool);
        assert(result == VK_SUCCESS);
    }

    void create_compute_descriptor_sets()
    {
        // gbuffer input descriptor
        VkDescriptorSetLayout layouts[] = {
            compute_input_descriptor_set_layout
        };
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = compute_descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = layouts;

        VkResult result = vkAllocateDescriptorSets(device, &alloc_info, &compute_input_descriptor_set);
        assert(result == VK_SUCCESS);

        VkDescriptorImageInfo input_image_infos[4] = {};
        VkDescriptorBufferInfo input_buffer_infos[2] = {};
        VkWriteDescriptorSet input_descriptor_writes[6] = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            input_image_infos[i].imageView = gbuffer.image_views[i];
            input_image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            input_image_infos[i].sampler = gbuffer.sampler;

            input_descriptor_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_descriptor_writes[i].dstSet = compute_input_descriptor_set;
            input_descriptor_writes[i].dstBinding = i;
            input_descriptor_writes[i].dstArrayElement = 0;
            input_descriptor_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_descriptor_writes[i].descriptorCount = 1;
            input_descriptor_writes[i].pBufferInfo = nullptr;
            input_descriptor_writes[i].pImageInfo = &input_image_infos[i];
            input_descriptor_writes[i].pTexelBufferView = nullptr;
        }

        input_buffer_infos[0].buffer = compute_uniform_buffer;
        input_buffer_infos[0].offset = 0;
        input_buffer_infos[0].range = sizeof(ubo_compute_t);

        input_buffer_infos[1].buffer = lights.storage_buffer;
        input_buffer_infos[1].offset = 0;
        input_buffer_infos[1].range = sizeof(light_t) * lights.num_lights;

        input_descriptor_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        input_descriptor_writes[4].dstSet = compute_input_descriptor_set;
        input_descriptor_writes[4].dstBinding = 4;
        input_descriptor_writes[4].dstArrayElement = 0;
        input_descriptor_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        input_descriptor_writes[4].descriptorCount = 1;
        input_descriptor_writes[4].pBufferInfo = &input_buffer_infos[0];
        input_descriptor_writes[4].pImageInfo = nullptr;
        input_descriptor_writes[4].pTexelBufferView = nullptr;

        input_descriptor_writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        input_descriptor_writes[5].dstSet = compute_input_descriptor_set;
        input_descriptor_writes[5].dstBinding = 5;
        input_descriptor_writes[5].dstArrayElement = 0;
        input_descriptor_writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        input_descriptor_writes[5].descriptorCount = 1;
        input_descriptor_writes[5].pBufferInfo = &input_buffer_infos[1];
        input_descriptor_writes[5].pImageInfo = nullptr;
        input_descriptor_writes[5].pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(device, 6, input_descriptor_writes, 0, nullptr);

        // shaded output descriptor
        compute_output_descriptor_sets.resize(swapchain_images.size());
        layouts[0] = compute_output_descriptor_set_layout;
        alloc_info.pSetLayouts = layouts;

        std::vector<VkWriteDescriptorSet> output_descriptor_writes(swapchain_images.size());
        std::vector<VkDescriptorImageInfo> output_image_infos(swapchain_images.size());
        for (uint32_t i = 0; i < swapchain_images.size(); ++i)
        {
            result = vkAllocateDescriptorSets(device, &alloc_info, &compute_output_descriptor_sets[i]);
            assert(result == VK_SUCCESS);

            output_image_infos[i].imageView = swapchain_image_views[i];
            output_image_infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            output_descriptor_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_descriptor_writes[i].dstSet = compute_output_descriptor_sets[i];
            output_descriptor_writes[i].dstBinding = 0;
            output_descriptor_writes[i].dstArrayElement = 0;
            output_descriptor_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_descriptor_writes[i].descriptorCount = 1;
            output_descriptor_writes[i].pBufferInfo = nullptr;
            output_descriptor_writes[i].pImageInfo = &output_image_infos[i];
            output_descriptor_writes[i].pTexelBufferView = nullptr;
        }

        vkUpdateDescriptorSets(device, output_descriptor_writes.size(), output_descriptor_writes.data(), 0, nullptr);
    }

    void create_shader_module_from_file(const char* filename, VkShaderModule* shader_module)
    {
        std::ifstream source(filename, std::ios::ate | std::ios::binary);
        assert(source.is_open());

        size_t file_size = (size_t) source.tellg();
        std::vector<char> buffer(file_size);

        source.seekg(0);
        source.read(buffer.data(), file_size);

        source.close();

        VkShaderModuleCreateInfo module_info = {};
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = file_size;
        module_info.pCode = (uint32_t*) buffer.data();

        VkResult result = vkCreateShaderModule(device, &module_info, nullptr, shader_module);
        assert(result == VK_SUCCESS);
    }

    void create_graphics_command_pool()
    {
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = graphics_queue_index;
        pool_info.flags = 0;

        VkResult result = vkCreateCommandPool(device, &pool_info, nullptr, &graphics_command_pool);
        assert(result == VK_SUCCESS);
    }

    void create_graphics_command_buffers()
    {
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = graphics_command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        VkResult result = vkAllocateCommandBuffers(device, &alloc_info, &graphics_command_buffer);
        assert(result == VK_SUCCESS);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        begin_info.pInheritanceInfo = nullptr;

        vkBeginCommandBuffer(graphics_command_buffer, &begin_info);

        VkRenderPassBeginInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = render_pass;
        render_pass_info.framebuffer = gbuffer.framebuffer;
        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = swapchain_extent;
        VkClearValue clear_values[4] = {};
        clear_values[0].color = {0.0f, 0.2f, 0.4f, 1.0f};
        clear_values[1].color = {0.0f, 0.2f, 0.4f, 1.0f};
        clear_values[2].color = {0.0f, 0.2f, 0.4f, 1.0f};
        clear_values[3].depthStencil = {1.0f, 0};

        render_pass_info.clearValueCount = 4;
        render_pass_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(graphics_command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(graphics_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

        VkBuffer vertex_buffers[] = {vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(graphics_command_buffer, 0, 1, vertex_buffers, offsets);

        vkCmdBindIndexBuffer(graphics_command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(graphics_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                graphics_pipeline_layout, 0, 1, &graphics_descriptor_set, 0, nullptr);

        vkCmdDrawIndexed(graphics_command_buffer, num_plane_indices, 1, 0, 0, 0);

        vkCmdEndRenderPass(graphics_command_buffer);

        result = vkEndCommandBuffer(graphics_command_buffer);
        assert(result == VK_SUCCESS);
    }

    void create_compute_command_pool()
    {
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = compute_queue_index;
        pool_info.flags = 0;

        VkResult result = vkCreateCommandPool(device, &pool_info, nullptr, &compute_command_pool);
        assert(result == VK_SUCCESS);
    }

    void create_compute_command_buffers()
    {
        compute_command_buffers.resize(swapchain_images.size());

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = compute_command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = (uint32_t) compute_command_buffers.size();

        VkResult result = vkAllocateCommandBuffers(device, &alloc_info, compute_command_buffers.data());
        assert(result == VK_SUCCESS);

        for (size_t i = 0; i < compute_command_buffers.size(); ++i)
        {
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            begin_info.pInheritanceInfo = nullptr;

            vkBeginCommandBuffer(compute_command_buffers[i], &begin_info);

            VkImageMemoryBarrier image_barrier = {};
            image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.image = swapchain_images[i];
            image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            image_barrier.srcAccessMask = 0;//VK_ACCESS_MEMORY_READ_BIT;
            image_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_barrier.subresourceRange.baseMipLevel = 0;
            image_barrier.subresourceRange.levelCount = 1;
            image_barrier.subresourceRange.baseArrayLayer = 0;
            image_barrier.subresourceRange.layerCount = 1;

            // the wait semaphore dictates that the swapchain image is only guaranteed to be
            // available at the compute stage, and the layout must only be transitioned
            // once the image has been successfully acquired.
            vkCmdPipelineBarrier(compute_command_buffers[i],
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // the image is available here
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // the image is used here
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &image_barrier);

            vkCmdBindPipeline(compute_command_buffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);

            VkDescriptorSet output_descriptor_sets[] = {
                compute_input_descriptor_set,
                compute_output_descriptor_sets[i]
            };
            vkCmdBindDescriptorSets(compute_command_buffers[i], VK_PIPELINE_BIND_POINT_COMPUTE,
                                    compute_pipeline_layout, 0, 2, output_descriptor_sets, 0, nullptr);

            vkCmdDispatch(compute_command_buffers[i], swapchain_extent.width / 16, swapchain_extent.height / 16, 1);

            image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            image_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            image_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

            // transition layout at end of pipe, suitable for
            // handing over to e.g. present according to the spec
            vkCmdPipelineBarrier(compute_command_buffers[i],
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &image_barrier);

            VkResult result = vkEndCommandBuffer(compute_command_buffers[i]);
            assert(result == VK_SUCCESS);
        }
    }

    void create_vertex_buffer()
    {
        VkDeviceSize buffer_size = sizeof(plane_vertices[0]) * num_plane_vertices;

        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_buffer, &staging_buffer_memory);

        void* data;
        vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
        memcpy(data, plane_vertices, (size_t) buffer_size);
        vkUnmapMemory(device, staging_buffer_memory);

        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &vertex_buffer, &vertex_buffer_memory);

        copy_buffer(staging_buffer, vertex_buffer, buffer_size);

        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_buffer_memory, nullptr);
    }

    void create_index_buffer()
    {
        VkDeviceSize buffer_size = sizeof(plane_indices[0]) * num_plane_indices;

        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_buffer, &staging_buffer_memory);

        void* data;
        vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
        memcpy(data, plane_indices, (size_t) buffer_size);
        vkUnmapMemory(device, staging_buffer_memory);

        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &index_buffer, &index_buffer_memory);

        copy_buffer(staging_buffer, index_buffer, buffer_size);

        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_buffer_memory, nullptr);
    }

    void create_uniform_buffers()
    {
        VkDeviceSize buffer_size = sizeof(ubo_transforms_t);
        create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &graphics_uniform_buffer, &graphics_uniform_buffer_memory);

        buffer_size = sizeof(ubo_compute_t);
        create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &compute_uniform_buffer, &compute_uniform_buffer_memory);
    }

    void init_lights()
    {
        lights.data = new light_t[lights.num_lights];
        lights.animation_offsets = new float[lights.num_lights];

        size_t size = sizeof(light_t) * lights.num_lights;
        VkDeviceSize buffer_size = size;
        create_buffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &lights.storage_buffer, &lights.storage_buffer_memory);

        srand(time(0));

        for (uint32_t i = 0; i < lights.num_lights; ++i)
        {
            float x = rand() / (float) RAND_MAX * 4.0f - 2.0f;
            float y = 1.0f;
            float z = rand() / (float) RAND_MAX * 4.0f - 2.0f;
            float r = rand() / (float) RAND_MAX;
            float g = rand() / (float) RAND_MAX;
            float b = rand() / (float) RAND_MAX;
            lights.data[i] = {
                {x, y, z, 1.0f},
                {r, g, b, 1.5f}
            };

            float t = rand() / (float) RAND_MAX * 2.0f;
            lights.animation_offsets[i] = t;
        }

        void* data;
        vkMapMemory(device, lights.storage_buffer_memory, 0, size, 0, &data);
        memcpy(data, lights.data, size);
        vkUnmapMemory(device, lights.storage_buffer_memory);
    }

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* buffer_memory)
    {
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, buffer);
        assert(result == VK_SUCCESS);

        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(device, *buffer, &memory_requirements);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = memory_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, properties);

        result = vkAllocateMemory(device, &alloc_info, nullptr, buffer_memory);
        assert(result == VK_SUCCESS);

        vkBindBufferMemory(device, *buffer, *buffer_memory, 0);
    }

    void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
    {
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = graphics_command_pool;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer;
        vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(command_buffer, &begin_info);

        VkBufferCopy copy_region = {};
        copy_region.srcOffset = 0;
        copy_region.dstOffset = 0;
        copy_region.size = size;

        vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

        vkEndCommandBuffer(command_buffer);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;

        vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue);

        vkFreeCommandBuffers(device, graphics_command_pool, 1, &command_buffer);
    }

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties)
    {
        for (uint32_t i = 0; i < physical_device_memory_properties.memoryTypeCount; ++i)
        {
            if ( (type_filter & (1 << i)) &&
                 (physical_device_memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        assert(false);
    }

    void create_sync_primitives()
    {
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkResult result = vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphore);
        assert(result == VK_SUCCESS);
        result = vkCreateSemaphore(device, &semaphore_info, nullptr, &gbuffer_finished_semaphore);
        assert(result == VK_SUCCESS);
        result = vkCreateSemaphore(device, &semaphore_info, nullptr, &compute_finished_semaphore);
        assert(result == VK_SUCCESS);

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = vkCreateFence(device, &fence_info, nullptr, &compute_finished_fence);
    }

    void draw_frame()
    {
        uint32_t image_index;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);

        // draw gbuffer
        vkWaitForFences(device, 1, &compute_finished_fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &compute_finished_fence);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 0;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &graphics_command_buffer;
        VkSemaphore graphics_signal_semaphores[] = {
            gbuffer_finished_semaphore
        };
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = graphics_signal_semaphores;

        VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        assert(result == VK_SUCCESS);

        // perform lighting
        VkSemaphore compute_wait_semaphores[] = {
            gbuffer_finished_semaphore,
            image_available_semaphore
        };
        VkPipelineStageFlags compute_wait_stages[] = {
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        };
        submit_info.waitSemaphoreCount = 2;
        submit_info.pWaitSemaphores = compute_wait_semaphores;
        submit_info.pWaitDstStageMask = compute_wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &compute_command_buffers[image_index];
        VkSemaphore compute_signal_semaphores[] = {
            compute_finished_semaphore
        };
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = compute_signal_semaphores;

        result = vkQueueSubmit(compute_queue, 1, &submit_info, compute_finished_fence);
        assert(result == VK_SUCCESS);

        // present to screen
        VkPresentInfoKHR present_info = {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = compute_signal_semaphores;
        VkSwapchainKHR swapchains[] = { swapchain };
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;
        present_info.pResults = nullptr;

        vkQueuePresentKHR(present_queue, &present_info);
    }

    void update_ubos(float frame_delta_us)
    {
        static glm::mat4 prev_rotation = glm::mat4();

        ubo_transforms_t transforms = {};
        transforms.model = glm::mat4();/*glm::rotate(prev_rotation,
                                       frame_delta_us / 1e6f * glm::radians(90.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));*/
        prev_rotation = transforms.model;
        transforms.view = glm::lookAt(glm::vec3(-3.0f, -3.0f, -3.0f),
                                      glm::vec3(0.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, -1.0f, 0.0f));
        transforms.projection = glm::perspective(glm::radians(45.0f),
                                                 swapchain_extent.width / (float) swapchain_extent.height,
                                                 0.1f, 10.0f);
        transforms.projection[1][1] *= -1;

        void* data;
        vkMapMemory(device, graphics_uniform_buffer_memory, 0, sizeof(transforms), 0, &data);
        memcpy(data, &transforms, sizeof(transforms));
        vkUnmapMemory(device, graphics_uniform_buffer_memory);

        ubo_compute_t ubo_compute;
        ubo_compute.view = transforms.view;
        ubo_compute.projection = transforms.projection;
        ubo_compute.num_lights = lights.num_lights;

        vkMapMemory(device, compute_uniform_buffer_memory, 0, sizeof(ubo_compute), 0, &data);
        memcpy(data, &ubo_compute, sizeof(ubo_compute));
        vkUnmapMemory(device, compute_uniform_buffer_memory);
    }

    void update_lights(float frame_delta_us)
    {
        float frame_delta_s = frame_delta_us / 1e6f;
        for (uint32_t i = 0; i < lights.num_lights; ++i)
        {
            lights.animation_offsets[i] += frame_delta_s;
            float wt = lights.animation_period * lights.animation_offsets[i];
            lights.data[i].position.y = 2.0f * sin(wt) + 2.0f;
        }

        size_t size = sizeof(light_t) * lights.num_lights;
        void* data;
        vkMapMemory(device, lights.storage_buffer_memory, 0, size, 0, &data);
        memcpy(data, lights.data, size);
        vkUnmapMemory(device, lights.storage_buffer_memory);
    }

    void clear_vulkan()
    {
        vkDeviceWaitIdle(device);

        vkDestroyBuffer(device, lights.storage_buffer, nullptr);
        vkFreeMemory(device, lights.storage_buffer_memory, nullptr);
        delete[] lights.data;
        delete[] lights.animation_offsets;

        vkDestroyFence(device, compute_finished_fence, nullptr);

        vkDestroySemaphore(device, compute_finished_semaphore, nullptr);
        vkDestroySemaphore(device, gbuffer_finished_semaphore, nullptr);
        vkDestroySemaphore(device, image_available_semaphore, nullptr);

        vkDestroyBuffer(device, vertex_buffer, nullptr);
        vkFreeMemory(device, vertex_buffer_memory, nullptr);
        vkDestroyBuffer(device, index_buffer, nullptr);
        vkFreeMemory(device, index_buffer_memory, nullptr);

        vkDestroyBuffer(device, graphics_uniform_buffer, nullptr);
        vkFreeMemory(device, graphics_uniform_buffer_memory, nullptr);
        vkDestroyBuffer(device, compute_uniform_buffer, nullptr);
        vkFreeMemory(device, compute_uniform_buffer_memory, nullptr);

        vkDestroyDescriptorPool(device, compute_descriptor_pool, nullptr);
        vkDestroyPipeline(device, compute_pipeline, nullptr);
        vkDestroyDescriptorSetLayout(device, compute_output_descriptor_set_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, compute_input_descriptor_set_layout, nullptr);
        vkDestroyPipelineLayout(device, compute_pipeline_layout, nullptr);

        vkDestroyDescriptorPool(device, graphics_descriptor_pool, nullptr);
        vkDestroyPipeline(device, graphics_pipeline, nullptr);
        vkDestroyDescriptorSetLayout(device, graphics_descriptor_set_layout, nullptr);
        vkDestroyPipelineLayout(device, graphics_pipeline_layout, nullptr);

        vkDestroyRenderPass(device, render_pass, nullptr);

        vkDestroyCommandPool(device, compute_command_pool, nullptr);
        vkDestroyCommandPool(device, graphics_command_pool, nullptr);

        vkDestroySampler(device, gbuffer.sampler, nullptr);
        vkDestroyFramebuffer(device, gbuffer.framebuffer, nullptr);
        for (uint32_t i = 0; i < 4; ++i)
        {
            vkDestroyImageView(device, gbuffer.image_views[i], nullptr);
            vkDestroyImage(device, gbuffer.images[i], nullptr);
            vkFreeMemory(device, gbuffer.image_memory[i], nullptr);
        }

        for (uint32_t i = 0; i < swapchain_image_views.size(); ++i)
        {
            vkDestroyImageView(device, swapchain_image_views[i], nullptr);

        }

        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);

        vkDestroyDevice(device, nullptr);

        auto fn = (PFN_vkDestroyDebugReportCallbackEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
        assert(fn);

        fn(instance, callback, nullptr);

        vkDestroyInstance(instance, nullptr);
    }
}
