#include "engine.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <array>

// Validation layers
#ifdef NDEBUG
static constexpr bool ENABLE_VALIDATION = false;
#else
static constexpr bool ENABLE_VALIDATION = true;
#endif

static const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
static constexpr uint32_t VALIDATION_LAYER_COUNT = 1;

static const char* DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
static constexpr uint32_t DEVICE_EXTENSION_COUNT = 1;

static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// Vertex structure
struct Vertex {
    float pos[2];
    float color[3];

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc = {};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attrs = {};
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, pos);
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, color);
        return attrs;
    }
};

// Hardcoded triangle vertices
static const Vertex TRIANGLE_VERTICES[] = {
    {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},  // Top - red
    {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},  // Bottom right - green
    {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},  // Bottom left - blue
};

// Global state
static SDL_Window* g_window = nullptr;
static char g_base_path[512] = {0};
static VkInstance g_instance = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT g_debug_messenger = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
static VkDevice g_device = VK_NULL_HANDLE;
static VkQueue g_graphics_queue = VK_NULL_HANDLE;
static VkQueue g_present_queue = VK_NULL_HANDLE;
static uint32_t g_graphics_family = 0;
static uint32_t g_present_family = 0;

// Swapchain
static VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
static VkFormat g_swapchain_format = VK_FORMAT_UNDEFINED;
static VkExtent2D g_swapchain_extent = {0, 0};
static std::vector<VkImage> g_swapchain_images;
static std::vector<VkImageView> g_swapchain_image_views;

// Render pass and framebuffers
static VkRenderPass g_render_pass = VK_NULL_HANDLE;
static std::vector<VkFramebuffer> g_framebuffers;

// Graphics pipeline
static VkPipelineLayout g_pipeline_layout = VK_NULL_HANDLE;
static VkPipeline g_graphics_pipeline = VK_NULL_HANDLE;

// Command pool and buffers
static VkCommandPool g_command_pool = VK_NULL_HANDLE;
static std::vector<VkCommandBuffer> g_command_buffers;

// Sync primitives
static std::vector<VkSemaphore> g_image_available_semaphores;
static std::vector<VkSemaphore> g_render_finished_semaphores;
static std::vector<VkFence> g_in_flight_fences;
static uint32_t g_current_frame = 0;

// Rendering mode
static bool g_draw_triangle = false;

// Debug callback
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        SDL_Log("Vulkan: %s", callback_data->pMessage);
    }
    return VK_FALSE;
}

static std::vector<char> read_file(const char* filename) {
    // Try multiple paths to find shaders
    const char* paths[] = {
        g_base_path,
        "native/build",
        "./native/build",
        "../native/build",
        "."
    };

    for (const char* base : paths) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/shaders/%s", base, filename);

        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), size);
            SDL_Log("Loaded shader: %s", path);
            return buffer;
        }
    }

    SDL_Log("Failed to find shader file: %s", filename);
    return {};
}

static VkShaderModule create_shader_module(const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    if (vkCreateShaderModule(g_device, &create_info, nullptr, &module) != VK_SUCCESS) {
        SDL_Log("Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    return module;
}

static bool check_validation_layer_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available.data());

    for (uint32_t i = 0; i < VALIDATION_LAYER_COUNT; i++) {
        bool found = false;
        for (const auto& layer : available) {
            if (strcmp(VALIDATION_LAYERS[i], layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static int create_instance() {
    if (ENABLE_VALIDATION && !check_validation_layer_support()) {
        SDL_Log("Validation layers requested but not available");
        return 1;
    }

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "HXO Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "HXO";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    uint32_t sdl_ext_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);
    if (ENABLE_VALIDATION) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
    if (ENABLE_VALIDATION) {
        create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
        create_info.ppEnabledLayerNames = VALIDATION_LAYERS;

        debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_create_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_create_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_create_info.pfnUserCallback = debug_callback;
        create_info.pNext = &debug_create_info;
    }

    if (vkCreateInstance(&create_info, nullptr, &g_instance) != VK_SUCCESS) {
        SDL_Log("Failed to create Vulkan instance");
        return 2;
    }

    if (ENABLE_VALIDATION) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(g_instance, "vkCreateDebugUtilsMessengerEXT");
        if (func && func(g_instance, &debug_create_info, nullptr, &g_debug_messenger) != VK_SUCCESS) {
            SDL_Log("Failed to create debug messenger");
        }
    }

    return 0;
}

static int create_surface() {
    if (!SDL_Vulkan_CreateSurface(g_window, g_instance, nullptr, &g_surface)) {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return 1;
    }
    return 0;
}

struct QueueFamilyIndices {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;
    bool complete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
};

static QueueFamilyIndices find_queue_families(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }
        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, g_surface, &present_support);
        if (present_support) {
            indices.present = i;
        }
        if (indices.complete()) break;
    }
    return indices;
}

static bool check_device_extension_support(VkPhysicalDevice device) {
    uint32_t count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    for (uint32_t i = 0; i < DEVICE_EXTENSION_COUNT; i++) {
        bool found = false;
        for (const auto& ext : available) {
            if (strcmp(DEVICE_EXTENSIONS[i], ext.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static int pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_instance, &count, nullptr);
    if (count == 0) {
        SDL_Log("No Vulkan-capable GPUs found");
        return 1;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(g_instance, &count, devices.data());

    for (const auto& device : devices) {
        auto indices = find_queue_families(device);
        if (!indices.complete()) continue;
        if (!check_device_extension_support(device)) continue;

        uint32_t format_count, mode_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_surface, &format_count, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_surface, &mode_count, nullptr);
        if (format_count == 0 || mode_count == 0) continue;

        g_physical_device = device;
        g_graphics_family = indices.graphics;
        g_present_family = indices.present;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        SDL_Log("Selected GPU: %s", props.deviceName);
        return 0;
    }

    SDL_Log("No suitable GPU found");
    return 2;
}

static int create_logical_device() {
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    float priority = 1.0f;

    std::vector<uint32_t> unique_families = {g_graphics_family};
    if (g_present_family != g_graphics_family) {
        unique_families.push_back(g_present_family);
    }

    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queue_create_infos.push_back(info);
    }

    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.pEnabledFeatures = &features;
    create_info.enabledExtensionCount = DEVICE_EXTENSION_COUNT;
    create_info.ppEnabledExtensionNames = DEVICE_EXTENSIONS;

    if (ENABLE_VALIDATION) {
        create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
        create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
    }

    if (vkCreateDevice(g_physical_device, &create_info, nullptr, &g_device) != VK_SUCCESS) {
        SDL_Log("Failed to create logical device");
        return 1;
    }

    vkGetDeviceQueue(g_device, g_graphics_family, 0, &g_graphics_queue);
    vkGetDeviceQueue(g_device, g_present_family, 0, &g_present_queue);
    return 0;
}

static VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    int w, h;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    VkExtent2D extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

static int create_swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device, g_surface, &caps);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device, g_surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device, g_surface, &format_count, formats.data());

    uint32_t mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_physical_device, g_surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_physical_device, g_surface, &mode_count, modes.data());

    auto surface_format = choose_surface_format(formats);
    auto present_mode = choose_present_mode(modes);
    auto extent = choose_extent(caps);

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = g_surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queue_families[] = {g_graphics_family, g_present_family};
    if (g_graphics_family != g_present_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_families;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    create_info.preTransform = caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(g_device, &create_info, nullptr, &g_swapchain) != VK_SUCCESS) {
        SDL_Log("Failed to create swapchain");
        return 1;
    }

    g_swapchain_format = surface_format.format;
    g_swapchain_extent = extent;

    vkGetSwapchainImagesKHR(g_device, g_swapchain, &image_count, nullptr);
    g_swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &image_count, g_swapchain_images.data());

    g_swapchain_image_views.resize(image_count);
    for (size_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = g_swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = g_swapchain_format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(g_device, &view_info, nullptr, &g_swapchain_image_views[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create image view");
            return 2;
        }
    }

    return 0;
}

static int create_render_pass() {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = g_swapchain_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    if (vkCreateRenderPass(g_device, &create_info, nullptr, &g_render_pass) != VK_SUCCESS) {
        SDL_Log("Failed to create render pass");
        return 1;
    }
    return 0;
}

static int create_graphics_pipeline() {
    auto vert_code = read_file("triangle.vert.spv");
    auto frag_code = read_file("triangle.frag.spv");

    if (vert_code.empty() || frag_code.empty()) {
        SDL_Log("Failed to load shaders");
        return 1;
    }

    VkShaderModule vert_module = create_shader_module(vert_code);
    VkShaderModule frag_module = create_shader_module(frag_code);

    if (!vert_module || !frag_module) {
        return 2;
    }

    VkPipelineShaderStageCreateInfo vert_stage = {};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage = {};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

    // Vertex input - empty, using hardcoded vertices in shader
    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 0;
    vertex_input.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    if (vkCreatePipelineLayout(g_device, &layout_info, nullptr, &g_pipeline_layout) != VK_SUCCESS) {
        SDL_Log("Failed to create pipeline layout");
        vkDestroyShaderModule(g_device, vert_module, nullptr);
        vkDestroyShaderModule(g_device, frag_module, nullptr);
        return 3;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = g_pipeline_layout;
    pipeline_info.renderPass = g_render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &g_graphics_pipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create graphics pipeline");
        vkDestroyShaderModule(g_device, vert_module, nullptr);
        vkDestroyShaderModule(g_device, frag_module, nullptr);
        return 4;
    }

    vkDestroyShaderModule(g_device, vert_module, nullptr);
    vkDestroyShaderModule(g_device, frag_module, nullptr);

    g_draw_triangle = true;
    SDL_Log("Graphics pipeline created successfully");
    return 0;
}

static int create_framebuffers() {
    g_framebuffers.resize(g_swapchain_image_views.size());

    for (size_t i = 0; i < g_swapchain_image_views.size(); i++) {
        VkFramebufferCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_info.renderPass = g_render_pass;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &g_swapchain_image_views[i];
        create_info.width = g_swapchain_extent.width;
        create_info.height = g_swapchain_extent.height;
        create_info.layers = 1;

        if (vkCreateFramebuffer(g_device, &create_info, nullptr, &g_framebuffers[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create framebuffer");
            return 1;
        }
    }
    return 0;
}

static int create_command_pool() {
    VkCommandPoolCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    create_info.queueFamilyIndex = g_graphics_family;

    if (vkCreateCommandPool(g_device, &create_info, nullptr, &g_command_pool) != VK_SUCCESS) {
        SDL_Log("Failed to create command pool");
        return 1;
    }
    return 0;
}

static int create_command_buffers() {
    g_command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = g_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(g_device, &alloc_info, g_command_buffers.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate command buffers");
        return 1;
    }
    return 0;
}

static int create_sync_objects() {
    g_image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    g_render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    g_in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(g_device, &sem_info, nullptr, &g_image_available_semaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(g_device, &sem_info, nullptr, &g_render_finished_semaphores[i]) != VK_SUCCESS ||
            vkCreateFence(g_device, &fence_info, nullptr, &g_in_flight_fences[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create sync objects");
            return 1;
        }
    }
    return 0;
}

static void cleanup_swapchain() {
    for (auto fb : g_framebuffers) {
        vkDestroyFramebuffer(g_device, fb, nullptr);
    }
    g_framebuffers.clear();

    for (auto view : g_swapchain_image_views) {
        vkDestroyImageView(g_device, view, nullptr);
    }
    g_swapchain_image_views.clear();

    if (g_swapchain) {
        vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
        g_swapchain = VK_NULL_HANDLE;
    }
}

static int recreate_swapchain() {
    int w, h;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    while (w == 0 || h == 0) {
        SDL_GetWindowSizeInPixels(g_window, &w, &h);
        SDL_WaitEvent(nullptr);
    }

    vkDeviceWaitIdle(g_device);
    cleanup_swapchain();

    if (create_swapchain() != 0) return 1;
    if (create_framebuffers() != 0) return 2;
    return 0;
}

extern "C" {

int engine_init(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Get base path for shader loading
    const char* base = SDL_GetBasePath();
    if (base) {
        strncpy(g_base_path, base, sizeof(g_base_path) - 1);
    } else {
        strcpy(g_base_path, ".");
    }

    g_window = SDL_CreateWindow(title, width, height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    if (create_instance() != 0) return 3;
    if (create_surface() != 0) return 4;
    if (pick_physical_device() != 0) return 5;
    if (create_logical_device() != 0) return 6;
    if (create_swapchain() != 0) return 7;
    if (create_render_pass() != 0) return 8;
    if (create_graphics_pipeline() != 0) return 9;
    if (create_framebuffers() != 0) return 10;
    if (create_command_pool() != 0) return 11;
    if (create_command_buffers() != 0) return 12;
    if (create_sync_objects() != 0) return 13;

    SDL_Log("Engine initialized with Vulkan: %s (%dx%d)", title, width, height);
    return 0;
}

void engine_shutdown(void) {
    if (g_device) {
        vkDeviceWaitIdle(g_device);
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (g_render_finished_semaphores.size() > (size_t)i)
            vkDestroySemaphore(g_device, g_render_finished_semaphores[i], nullptr);
        if (g_image_available_semaphores.size() > (size_t)i)
            vkDestroySemaphore(g_device, g_image_available_semaphores[i], nullptr);
        if (g_in_flight_fences.size() > (size_t)i)
            vkDestroyFence(g_device, g_in_flight_fences[i], nullptr);
    }

    if (g_command_pool) vkDestroyCommandPool(g_device, g_command_pool, nullptr);

    cleanup_swapchain();

    if (g_graphics_pipeline) vkDestroyPipeline(g_device, g_graphics_pipeline, nullptr);
    if (g_pipeline_layout) vkDestroyPipelineLayout(g_device, g_pipeline_layout, nullptr);
    if (g_render_pass) vkDestroyRenderPass(g_device, g_render_pass, nullptr);
    if (g_device) vkDestroyDevice(g_device, nullptr);

    if (ENABLE_VALIDATION && g_debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(g_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(g_instance, g_debug_messenger, nullptr);
    }

    if (g_surface) vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
    if (g_instance) vkDestroyInstance(g_instance, nullptr);

    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    SDL_Log("Engine shutdown complete");
}

bool engine_poll_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            return true;
        }
    }
    return false;
}

uint64_t engine_get_ticks(void) {
    return SDL_GetTicks();
}

int engine_render_frame(float r, float g, float b, float a) {
    vkWaitForFences(g_device, 1, &g_in_flight_fences[g_current_frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
        g_image_available_semaphores[g_current_frame], VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return 1;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        SDL_Log("Failed to acquire swapchain image");
        return 2;
    }

    vkResetFences(g_device, 1, &g_in_flight_fences[g_current_frame]);

    VkCommandBuffer cmd = g_command_buffers[g_current_frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkClearValue clear_value = {{{r, g, b, a}}};

    VkRenderPassBeginInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = g_render_pass;
    rp_info.framebuffer = g_framebuffers[image_index];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = g_swapchain_extent;
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    if (g_draw_triangle && g_graphics_pipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_graphics_pipeline);

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(g_swapchain_extent.width);
        viewport.height = static_cast<float>(g_swapchain_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = g_swapchain_extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Draw triangle with hardcoded vertices in shader
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSemaphore wait_semaphores[] = {g_image_available_semaphores[g_current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[] = {g_render_finished_semaphores[g_current_frame]};

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(g_graphics_queue, 1, &submit_info, g_in_flight_fences[g_current_frame]) != VK_SUCCESS) {
        SDL_Log("Failed to submit draw command buffer");
        return 3;
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &g_swapchain;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(g_present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else if (result != VK_SUCCESS) {
        SDL_Log("Failed to present");
        return 4;
    }

    g_current_frame = (g_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    return 0;
}

int engine_handle_resize(void) {
    return recreate_swapchain();
}

} // extern "C"
