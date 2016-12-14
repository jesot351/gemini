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

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace SRendering
{
    VkInstance instance;
    VkDebugReportCallbackEXT callback;
    VkSurfaceKHR surface;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device;

    VkQueue graphics_queue;
    VkQueue present_queue;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    std::vector<VkFramebuffer> swapchain_framebuffers;

    VkRenderPass render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[2];

    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;

    VkDeviceMemory vertex_buffer_memory;
    VkBuffer vertex_buffer;
    VkDeviceMemory index_buffer_memory;
    VkBuffer index_buffer;
    VkDeviceMemory uniform_buffer_memory;
    VkBuffer uniform_buffer;

    VkDeviceMemory overlay_vertex_buffer_memory;
    VkBuffer overlay_vertex_buffer;
    VkDeviceMemory overlay_index_buffer_memory;
    VkBuffer overlay_index_buffer;

    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;

    const char* validation_layers[] = {
        "VK_LAYER_LUNARG_standard_validation"
    };

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    const uint32_t num_vertices = 4;
    const vertex_t vertices[num_vertices] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    const uint32_t num_indices = 6;
    const uint32_t indices[num_indices] = {
        0, 1, 2, 2, 3, 0
    };

    const uint32_t num_overlay_vertices = 4 * 1024; // max 1024 tasks. should be enough
    void* mapped_overlay_vertex_buffer;
    const uint32_t num_overlay_indices = num_overlay_vertices / 4 * 6;

    void init_vulkan(GLFWwindow* window)
    {
        create_instance();
        setup_debug_callback();
        create_surface(window);
        pick_physical_device();
        create_logical_device();
        create_swapchain();
        create_image_views();
        create_render_pass();
        create_descriptor_set_layout();
        create_graphics_pipeline();
        create_framebuffers();
        create_command_pool();
        create_vertex_buffer();
        create_index_buffer();
        create_uniform_buffer();
        create_descriptor_pool();
        create_descriptor_set();
        create_command_buffers();
        create_semaphores();
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
        assert(num_devices != 0);
        std::vector<VkPhysicalDevice> devices(num_devices);
        vkEnumeratePhysicalDevices(instance, &num_devices, devices.data());

        for (const VkPhysicalDevice& device : devices)
        {
            if (is_device_suitable(device))
            {
                physical_device = device;
                break;
            }
        }

        assert(physical_device != VK_NULL_HANDLE);
    }

    bool is_device_suitable(VkPhysicalDevice physical_device)
    {
        queue_family_indices_t indices = find_queue_families(physical_device);

        bool extensions_supported = check_device_extension_support(physical_device);

        bool swapchain_adequate = false;
        if (extensions_supported)
        {
            swapchain_support_details_t details = query_swapchain_support(physical_device);
            swapchain_adequate = !details.formats.empty() && !details.present_modes.empty();
        }

        return indices.is_complete() && extensions_supported && swapchain_adequate;
    }

    queue_family_indices_t find_queue_families(VkPhysicalDevice physical_device)
    {
        queue_family_indices_t indices;

        uint32_t num_queue_families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_queue_families, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(num_queue_families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_queue_families, queue_families.data());

        for (uint32_t i = 0; i < queue_families.size(); ++i)
        {
            if (queue_families[i].queueCount > 0 &&
                queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphics = i;
            }

            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support);

            if (queue_families[i].queueCount > 0 && present_support)
            {
                indices.present = i;
            }

            if (indices.is_complete())
            {
                break;
            }
        }

        return indices;
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
        queue_family_indices_t indices = find_queue_families(physical_device);

        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        std::set<int32_t> unique_queue_families = {indices.graphics, indices.present};
        float queue_priority = 1.0f;

        for (int32_t queue_family : unique_queue_families)
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
        device_info.pQueueCreateInfos = queue_infos.data();
        device_info.queueCreateInfoCount = (uint32_t) queue_infos.size();
        device_info.pEnabledFeatures = &device_features;
        device_info.enabledExtensionCount = device_extensions.size();
        device_info.ppEnabledExtensionNames = device_extensions.data();
        device_info.enabledLayerCount = 1;
        device_info.ppEnabledLayerNames = validation_layers;

        VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
        assert(result == VK_SUCCESS);

        vkGetDeviceQueue(device, indices.graphics, 0, &graphics_queue);
        vkGetDeviceQueue(device, indices.present, 0, &present_queue);
    }

    void create_swapchain()
    {
        swapchain_support_details_t details = query_swapchain_support(physical_device);

        VkSurfaceFormatKHR surface_format = choose_swap_surface_format(&details.formats);
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
        sc_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        queue_family_indices_t indices = find_queue_families(physical_device);
        uint32_t queue_family_indices[] = {
            (uint32_t) indices.graphics,
            (uint32_t) indices.present
        };

        if (indices.graphics != indices.present)
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
    }

    swapchain_support_details_t query_swapchain_support(VkPhysicalDevice physical_device)
    {
        swapchain_support_details_t details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &details.capabilities);

        uint32_t num_formats = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &num_formats, nullptr);

        if (num_formats != 0)
        {
            details.formats.resize(num_formats);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &num_formats, details.formats.data());
        }

        uint32_t num_present_modes = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &num_present_modes, nullptr);

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
            VkExtent2D actual_extent = {WIDTH, HEIGHT};
            actual_extent.width = std::max(capabilities->minImageExtent.width,
                                           std::min(capabilities->maxImageExtent.width, actual_extent.width));
            actual_extent.height = std::max(capabilities->minImageExtent.height,
                                           std::min(capabilities->maxImageExtent.height, actual_extent.height));

            return actual_extent;
        }
    }

    void create_image_views()
    {
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

    void create_render_pass()
    {
        VkAttachmentDescription color_attachment = {};
        color_attachment.format = swapchain_image_format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref = {};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &color_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        VkResult result = vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass);
        assert(result == VK_SUCCESS);
    }

    void create_descriptor_set_layout()
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

        VkResult result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout);
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
        std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions = vertex_t::get_attribute_descriptions();

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

        VkPipelineColorBlendAttachmentState blend_attachment_info = {};
        blend_attachment_info.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        blend_attachment_info.blendEnable = VK_FALSE;
        blend_attachment_info.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment_info.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment_info.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment_info.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment_info.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment_info.alphaBlendOp = VK_BLEND_OP_ADD;


        VkPipelineColorBlendStateCreateInfo blend_info = {};
        blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend_info.logicOpEnable = VK_FALSE;
        blend_info.logicOp = VK_LOGIC_OP_COPY;
        blend_info.attachmentCount = 1;
        blend_info.pAttachments = &blend_attachment_info;
        blend_info.blendConstants[0] = 0.0f;
        blend_info.blendConstants[1] = 0.0f;
        blend_info.blendConstants[2] = 0.0f;
        blend_info.blendConstants[3] = 0.0f;

        VkDescriptorSetLayout set_layouts[] = { descriptor_set_layout };
        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 0;
        layout_info.pPushConstantRanges = nullptr;

        VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout);
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
        pipeline_info.pDepthStencilState = nullptr;
        pipeline_info.pColorBlendState = &blend_info;
        pipeline_info.pDynamicState = nullptr;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_info.basePipelineIndex = -1;

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline);
        assert(result == VK_SUCCESS);

        vkDestroyShaderModule(device, vertex_shader_module, nullptr);
        vkDestroyShaderModule(device, fragment_shader_module, nullptr);
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

    void create_framebuffers()
    {
        swapchain_framebuffers.resize(swapchain_image_views.size());

        for (size_t i = 0; i < swapchain_image_views.size(); ++i)
        {
            VkImageView attachments[] = {
                swapchain_image_views[i]
            };

            VkFramebufferCreateInfo framebuffer_info = {};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = attachments;
            framebuffer_info.width = swapchain_extent.width;
            framebuffer_info.height = swapchain_extent.height;
            framebuffer_info.layers = 1;

            VkResult result = vkCreateFramebuffer(device, &framebuffer_info, nullptr, &swapchain_framebuffers[i]);
            assert(result == VK_SUCCESS);
        }
    }

    void create_command_pool()
    {
        queue_family_indices_t indices = find_queue_families(physical_device);

        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = indices.graphics;
        pool_info.flags = 0;

        VkResult result = vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
        assert(result == VK_SUCCESS);
    }

    void create_command_buffers()
    {
        command_buffers.resize(swapchain_framebuffers.size());

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = (uint32_t) command_buffers.size();

        VkResult result = vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data());
        assert(result == VK_SUCCESS);

        for (size_t i = 0; i < command_buffers.size(); ++i)
        {
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            begin_info.pInheritanceInfo = nullptr;

            vkBeginCommandBuffer(command_buffers[i], &begin_info);

            VkRenderPassBeginInfo render_pass_info = {};
            render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_pass_info.renderPass = render_pass;
            render_pass_info.framebuffer = swapchain_framebuffers[i];
            render_pass_info.renderArea.offset = {0, 0};
            render_pass_info.renderArea.extent = swapchain_extent;
            VkClearValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            render_pass_info.clearValueCount = 1;
            render_pass_info.pClearValues = &clear_color;

            vkCmdBeginRenderPass(command_buffers[i], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

            // draw scene
            VkBuffer vertex_buffers[] = {vertex_buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(command_buffers[i], 0, 1, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(command_buffers[i], index_buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdBindDescriptorSets(command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout, 0, 1, &descriptor_sets[0], 0, nullptr);

            vkCmdDrawIndexed(command_buffers[i], num_indices, 1, 0, 0, 0);

            // draw overlay
            vertex_buffers[0] = overlay_vertex_buffer;
            offsets[0] = 0;
            vkCmdBindVertexBuffers(command_buffers[i], 0, 1, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(command_buffers[i], overlay_index_buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdBindDescriptorSets(command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout, 0, 1, &descriptor_sets[1], 0, nullptr);

            vkCmdDrawIndexed(command_buffers[i], num_overlay_indices, 1, 0, 0, 0);

            vkCmdEndRenderPass(command_buffers[i]);

            VkResult result = vkEndCommandBuffer(command_buffers[i]);
            assert(result == VK_SUCCESS);
        }
    }

    void create_vertex_buffer()
    {
        // regular vertex buffer
        VkDeviceSize buffer_size = sizeof(vertex_t) * num_vertices;

        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_buffer, &staging_buffer_memory);

        void* data;
        vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
        memcpy(data, vertices, (size_t) buffer_size);
        vkUnmapMemory(device, staging_buffer_memory);

        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &vertex_buffer, &vertex_buffer_memory);

        copy_buffer(staging_buffer, vertex_buffer, buffer_size);

        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_buffer_memory, nullptr);

        // overlay vertex buffer
        buffer_size = sizeof(vertex_t) * num_overlay_vertices;

        create_buffer(buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &overlay_vertex_buffer, &overlay_vertex_buffer_memory);

        vkMapMemory(device, overlay_vertex_buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped_overlay_vertex_buffer);
    }

    void create_index_buffer()
    {
        VkDeviceSize buffer_size = sizeof(indices[0]) * num_indices;
        VkDeviceSize overlay_buffer_size = sizeof(indices[0]) * num_overlay_indices;
        VkDeviceSize staging_buffer_size = std::max(buffer_size, overlay_buffer_size);

        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        create_buffer(staging_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_buffer, &staging_buffer_memory);

        // regular index buffer
        void* data;
        vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
        memcpy(data, indices, (size_t) buffer_size);
        vkUnmapMemory(device, staging_buffer_memory);

        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &index_buffer, &index_buffer_memory);

        copy_buffer(staging_buffer, index_buffer, buffer_size);

        // overlay index buffer
        uint32_t* overlay_indices = new uint32_t[num_overlay_indices];
        for (uint32_t i = 0; i < num_overlay_vertices / 4; ++i)
        {
            overlay_indices[i * 6 + 0] = i * 4 + 0;
            overlay_indices[i * 6 + 1] = i * 4 + 1;
            overlay_indices[i * 6 + 2] = i * 4 + 2;
            overlay_indices[i * 6 + 3] = i * 4 + 2;
            overlay_indices[i * 6 + 4] = i * 4 + 3;
            overlay_indices[i * 6 + 5] = i * 4 + 0;
        }

        vkMapMemory(device, staging_buffer_memory, 0, overlay_buffer_size, 0, &data);
        memcpy(data, overlay_indices, (size_t) overlay_buffer_size);
        vkUnmapMemory(device, staging_buffer_memory);

        delete[] overlay_indices;

        create_buffer(overlay_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &overlay_index_buffer, &overlay_index_buffer_memory);

        copy_buffer(staging_buffer, overlay_index_buffer, overlay_buffer_size);

        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_buffer_memory, nullptr);
    }

    void create_uniform_buffer()
    {
        VkDeviceSize buffer_size = sizeof(ubo_transforms_t);
        VkDeviceSize total_buffer_size = buffer_size * 2;
        create_buffer(total_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &uniform_buffer, &uniform_buffer_memory);

        ubo_transforms_t identity_transforms;
        identity_transforms.model = glm::mat4();
        identity_transforms.view = glm::mat4();
        identity_transforms.projection = glm::mat4();

        void* data;
        vkMapMemory(device, uniform_buffer_memory, buffer_size, buffer_size, 0, &data);
        memcpy(data, &identity_transforms, (size_t) buffer_size);
        vkUnmapMemory(device, uniform_buffer_memory);
    }

    void create_descriptor_pool()
    {
        VkDescriptorPoolSize pool_size = {};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = 2;

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 2;

        VkResult result = vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool);
        assert(result == VK_SUCCESS);
    }

    void create_descriptor_set()
    {
        VkDescriptorSetLayout layouts[2] = {
            descriptor_set_layout,
            descriptor_set_layout,
        };
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 2;
        alloc_info.pSetLayouts = layouts;

        VkResult result = vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets);
        assert(result == VK_SUCCESS);

        VkDescriptorBufferInfo buffer_infos[2] = {};
        buffer_infos[0].buffer = uniform_buffer;
        buffer_infos[0].offset = 0;
        buffer_infos[0].range = sizeof(ubo_transforms_t);

        buffer_infos[1].buffer = uniform_buffer;
        buffer_infos[1].offset = sizeof(ubo_transforms_t);
        buffer_infos[1].range = sizeof(ubo_transforms_t);

        VkWriteDescriptorSet descriptor_writes[2] = {};
        descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[0].dstSet = descriptor_sets[0];
        descriptor_writes[0].dstBinding = 0;
        descriptor_writes[0].dstArrayElement = 0;
        descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_writes[0].descriptorCount = 1;
        descriptor_writes[0].pBufferInfo = &buffer_infos[0];
        descriptor_writes[0].pImageInfo = nullptr;
        descriptor_writes[0].pTexelBufferView = nullptr;

        descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[1].dstSet = descriptor_sets[1];
        descriptor_writes[1].dstBinding = 0;
        descriptor_writes[1].dstArrayElement = 0;
        descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_writes[1].descriptorCount = 1;
        descriptor_writes[1].pBufferInfo = &buffer_infos[1];
        descriptor_writes[1].pImageInfo = nullptr;
        descriptor_writes[1].pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(device, 2, descriptor_writes, 0, nullptr);
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
        alloc_info.commandPool = command_pool;
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

        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    }

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
        {
            if ( (type_filter & (1 << i)) &&
                 (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        assert(false);
    }

    void create_semaphores()
    {
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkResult result = vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphore);
        assert(result == VK_SUCCESS);
        result = vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphore);
        assert(result == VK_SUCCESS);
    }

    void draw_frame()
    {
        uint32_t image_index;
        vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<uint64_t>::max(), image_available_semaphore, VK_NULL_HANDLE, &image_index);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore wait_semaphores[] = {image_available_semaphore};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers[image_index];
        VkSemaphore signal_semaphores[] = {render_finished_semaphore};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        assert(result == VK_SUCCESS);

        VkPresentInfoKHR present_info = {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        VkSwapchainKHR swapchains[] = {swapchain};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;
        present_info.pResults = nullptr;

        vkQueuePresentKHR(present_queue, &present_info);
    }

    void update_transforms(float frame_delta_ms)
    {
        static glm::mat4 prev_rotation = glm::mat4();

        ubo_transforms_t transforms = {};
        transforms.model = glm::rotate(prev_rotation,
                                       frame_delta_ms / 1e6f * glm::radians(90.0f),
                                       glm::vec3(0.0f, 0.0f, 1.0f));
        prev_rotation = transforms.model;
        transforms.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                                      glm::vec3(0.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, 1.0f));
        transforms.projection = glm::perspective(glm::radians(45.0f),
                                                 swapchain_extent.width / (float) swapchain_extent.height,
                                                 0.1f, 10.0f);
        transforms.projection[1][1] *= -1;

        void* data;
        vkMapMemory(device, uniform_buffer_memory, 0, sizeof(transforms), 0, &data);
        memcpy(data, &transforms, sizeof(transforms));
        vkUnmapMemory(device, uniform_buffer_memory);
    }

    void* get_mapped_overlay_vertex_buffer()
    {
        return mapped_overlay_vertex_buffer;
    }

    void clear_vulkan()
    {
        vkDeviceWaitIdle(device);

        vkDestroySemaphore(device, render_finished_semaphore, nullptr);
        vkDestroySemaphore(device, image_available_semaphore, nullptr);

        vkDestroyBuffer(device, vertex_buffer, nullptr);
        vkFreeMemory(device, vertex_buffer_memory, nullptr);
        vkDestroyBuffer(device, index_buffer, nullptr);
        vkFreeMemory(device, index_buffer_memory, nullptr);
        vkDestroyBuffer(device, uniform_buffer, nullptr);
        vkFreeMemory(device, uniform_buffer_memory, nullptr);

        vkUnmapMemory(device, overlay_vertex_buffer_memory);
        vkDestroyBuffer(device, overlay_vertex_buffer, nullptr);
        vkFreeMemory(device, overlay_vertex_buffer_memory, nullptr);
        vkDestroyBuffer(device, overlay_index_buffer, nullptr);
        vkFreeMemory(device, overlay_index_buffer_memory, nullptr);

        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

        vkDestroyCommandPool(device, command_pool, nullptr);

        vkDestroyPipeline(device, graphics_pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);

        vkDestroyRenderPass(device, render_pass, nullptr);

        for (uint32_t i = 0; i < swapchain_framebuffers.size(); ++i)
        {
            vkDestroyFramebuffer(device, swapchain_framebuffers[i], nullptr);

        }

        for (uint32_t i = 0; i < swapchain_image_views.size(); ++i)
        {
            vkDestroyImageView(device, swapchain_image_views[i], nullptr);

        }

        vkDestroySwapchainKHR(device, swapchain, nullptr);

        vkDestroyDevice(device, nullptr);

        vkDestroySurfaceKHR(instance, surface, nullptr);

        auto fn = (PFN_vkDestroyDebugReportCallbackEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
        assert(fn);
        fn(instance, callback, nullptr);

        vkDestroyInstance(instance, nullptr);
    }
}
