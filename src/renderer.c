#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <novacube/macros.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1001000
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_NULLABLE_NON_DISPATCHABLE
#define VMA_NOT_NULL_NON_DISPATCHABLE
NC_IGNORE_ALL_WARNINGS_BEGIN
#include <vk_mem_alloc.h>
NC_IGNORE_ALL_WARNINGS_END

#include <novacube/build_info.h>
#include <novacube/error_handling.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>

#ifdef ANDROID
#define NC__RENDERER_ASTC_TEXTURES 1
#define NC__RENDERER_ASSETS_BASE_PATH ""
#define NC__RENDERER_TEXTURE_EXTENSION ".astc"
#else
#define NC__RENDERER_ASTC_TEXTURES 0
#define NC__RENDERER_ASSETS_BASE_PATH "assets/"
#define NC__RENDERER_TEXTURE_EXTENSION ".png"
#endif

#define NC__RENDERER_VK_API_VERSION VK_API_VERSION_1_1
#define NC__RENDERER_INITIAL_TRANSFER_CAPACITY 65536u
#define NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS 4096
#define NC__RENDERER_CLEAR_RED 0.53f
#define NC__RENDERER_CLEAR_GREEN 0.81f
#define NC__RENDERER_CLEAR_BLUE 0.92f
#define NC__RENDERER_DEPTH_FORMAT VK_FORMAT_D16_UNORM

#define NC__CHECK_VK_RESULT(result_) do { \
    const VkResult nc__vk_result = (result_); \
    if (nc__vk_result != VK_SUCCESS) { \
        NC_SET_ERROR("%s failed with %s.", #result_, nc__renderer_vk_result_string(nc__vk_result)); \
        goto error; \
    } \
} while (false)

static const char* nc__renderer_crosshair_texture_path =
    NC__RENDERER_ASSETS_BASE_PATH "textures/gui/crosshair" NC__RENDERER_TEXTURE_EXTENSION;

typedef uint8_t nc__renderer_upload_kind_t;
enum {
    NC__RENDERER_UPLOAD_BUFFER = 1,
    NC__RENDERER_UPLOAD_TEXTURE,
};

typedef struct nc__renderer_texture_file_data_t {
    uint8_t* bytes;
    VkFormat format;
    uint32_t size;
    int16_t width;
    int16_t height;
} nc__renderer_texture_file_data_t;

typedef struct nc__renderer_upload_op_t {
    union {
        nc_renderer_buffer_t* buffer;
        struct {
            nc_renderer_texture_t* texture;
            uint16_t layer;
        } texture;
    };
    uint32_t source_offset;
    uint32_t size;
    nc__renderer_upload_kind_t kind;
} nc__renderer_upload_op_t;

typedef struct nc__renderer_astc_header_t {
    uint8_t magic[4];
    uint8_t block_x;
    uint8_t block_y;
    uint8_t block_z;
    uint8_t dim_x[3];
    uint8_t dim_y[3];
    uint8_t dim_z[3];
} nc__renderer_astc_header_t;

typedef struct nc__renderer_retired_buffer_t {
    VkBuffer buffer;
    VmaAllocation allocation;
} nc__renderer_retired_buffer_t;

typedef struct nc_renderer_texture_t {
    VkImage image;
    VmaAllocation allocation;
    VkImageView image_view;
    VkFormat format;
    VkImageLayout layout;
    int32_t width;
    int32_t height;
    uint16_t layer_count;
    bool is_array;
} nc_renderer_texture_t;

typedef struct nc_renderer_buffer_t {
    VkBuffer buffer;
    VmaAllocation allocation;
    VkBufferUsageFlags usage;
    uint32_t capacity;
    uint64_t queued_upload_frame;
} nc_renderer_buffer_t;

typedef struct nc__renderer_procedural_overlay_uniforms_t {
    float rings[2][4];
    float sticks[2][4];
    float crosshair[4];
} nc__renderer_procedural_overlay_uniforms_t;

typedef struct nc__renderer_block_highlight_vertex_uniforms_t {
    vkm_mat4 view_projection;
    vkm_vec4 block_position_and_scale;
} nc__renderer_block_highlight_vertex_uniforms_t;

typedef struct nc__renderer_block_highlight_fragment_uniforms_t {
    vkm_vec4 color;
    float time;
} nc__renderer_block_highlight_fragment_uniforms_t;

typedef struct nc__renderer_chunk_uniforms_t {
    vkm_mat4 view_projection;
    vkm_vec3 position;
} nc__renderer_chunk_uniforms_t;

typedef struct nc_renderer_t {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;
    VmaAllocator allocator;

    SDL_Window* window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkColorSpaceKHR swapchain_color_space;
    VkExtent2D swapchain_extent;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    VkFramebuffer* framebuffers;
    uint32_t swapchain_image_count;
    vkm_usvec2 window_size;
    vkm_usvec2 viewport;
    bool foreground;
    bool swapchain_dirty;
    bool surface_dirty;

    VkImage depth_image;
    VmaAllocation depth_allocation;
    VkImageView depth_image_view;

    VkRenderPass render_pass;
    VkDescriptorSetLayout descriptor_set_layouts[4];
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool frame_descriptor_pool;
    uint32_t frame_descriptor_set_count;

    VkPipeline chunk_pipeline;
    VkPipeline gui_pipeline;
    VkPipeline procedural_overlay_invert_pipeline;
    VkPipeline procedural_overlay_stick_pipeline;
    VkPipeline outline_block_highlight_pipeline;
    VkPipeline vignette_block_highlight_pipeline;
    VkPipeline plasma_block_highlight_pipeline;
    VkSampler chunk_sampler;
    VkSampler gui_sampler;
    nc_renderer_texture_t* procedural_overlay_crosshair_texture;
    nc_renderer_buffer_t* dummy_storage_buffer;

    VkCommandPool command_pool;
    VkCommandBuffer frame_command_buffer;
    VkFence frame_fence;
    VkSemaphore image_available_semaphore;
    VkSemaphore* render_finished_semaphores;
    bool frame_in_progress;
    bool frame_fence_pending;
    bool frame_has_swapchain_image;
    uint32_t frame_swapchain_image_index;

    VkBuffer transfer_buffer;
    VmaAllocation transfer_allocation;
    void* mapped_transfer_buffer;
    uint32_t transfer_size;
    uint32_t transfer_capacity;

    nc__renderer_retired_buffer_t* retired_transfer_buffers;
    uint32_t retired_transfer_buffer_count;
    uint32_t retired_transfer_buffer_capacity;

    nc__renderer_upload_op_t* upload_ops;
    uint32_t upload_count;
    uint32_t upload_capacity;
    bool uploads_dirty;

    uint64_t frame_id;
} nc_renderer_t;

#define TDS_IMPLEMENT
#define TDS_VALUE_T nc_renderer_chunk_opaque_draw_t
#define TDS_TYPE nc_renderer_chunk_opaque_draw_vec
#include <tds/vector.h>

static const char* nc__renderer_vk_result_string(const VkResult result) {
#define NC__VULKAN_ERROR_CASE(x) case x: return #x
    switch (result) {
        NC__VULKAN_ERROR_CASE(VK_SUCCESS);
        NC__VULKAN_ERROR_CASE(VK_NOT_READY);
        NC__VULKAN_ERROR_CASE(VK_TIMEOUT);
        NC__VULKAN_ERROR_CASE(VK_EVENT_SET);
        NC__VULKAN_ERROR_CASE(VK_EVENT_RESET);
        NC__VULKAN_ERROR_CASE(VK_INCOMPLETE);
        NC__VULKAN_ERROR_CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
        NC__VULKAN_ERROR_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INITIALIZATION_FAILED);
        NC__VULKAN_ERROR_CASE(VK_ERROR_DEVICE_LOST);
        NC__VULKAN_ERROR_CASE(VK_ERROR_MEMORY_MAP_FAILED);
        NC__VULKAN_ERROR_CASE(VK_ERROR_LAYER_NOT_PRESENT);
        NC__VULKAN_ERROR_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
        NC__VULKAN_ERROR_CASE(VK_ERROR_FEATURE_NOT_PRESENT);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
        NC__VULKAN_ERROR_CASE(VK_ERROR_TOO_MANY_OBJECTS);
        NC__VULKAN_ERROR_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
        NC__VULKAN_ERROR_CASE(VK_ERROR_FRAGMENTED_POOL);
        NC__VULKAN_ERROR_CASE(VK_ERROR_UNKNOWN);
        NC__VULKAN_ERROR_CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
        NC__VULKAN_ERROR_CASE(VK_ERROR_FRAGMENTATION);
        NC__VULKAN_ERROR_CASE(VK_PIPELINE_COMPILE_REQUIRED);
        NC__VULKAN_ERROR_CASE(VK_ERROR_SURFACE_LOST_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        NC__VULKAN_ERROR_CASE(VK_SUBOPTIMAL_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_OUT_OF_DATE_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INVALID_SHADER_NV);
        NC__VULKAN_ERROR_CASE(VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
        NC__VULKAN_ERROR_CASE(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
        NC__VULKAN_ERROR_CASE(VK_THREAD_IDLE_KHR);
        NC__VULKAN_ERROR_CASE(VK_THREAD_DONE_KHR);
        NC__VULKAN_ERROR_CASE(VK_OPERATION_DEFERRED_KHR);
        NC__VULKAN_ERROR_CASE(VK_OPERATION_NOT_DEFERRED_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_COMPRESSION_EXHAUSTED_EXT);
#ifndef ANDROID
        // The Vulkan SDK included in my NDK doesn't have these...
        NC__VULKAN_ERROR_CASE(VK_ERROR_VALIDATION_FAILED);
        NC__VULKAN_ERROR_CASE(VK_ERROR_NOT_PERMITTED);
        NC__VULKAN_ERROR_CASE(VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT);
        NC__VULKAN_ERROR_CASE(VK_INCOMPATIBLE_SHADER_BINARY_EXT);
        NC__VULKAN_ERROR_CASE(VK_PIPELINE_BINARY_MISSING_KHR);
        NC__VULKAN_ERROR_CASE(VK_ERROR_NOT_ENOUGH_SPACE_KHR);
#endif
        default:
            return "unknown VkResult";
    }
}
#undef NC__VULKAN_ERROR_CASE

static uint32_t nc__renderer_next_capacity(const uint32_t current, const uint32_t required, const uint32_t minimum) {
    uint32_t capacity = current ? current : minimum;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2) {
            capacity = UINT32_MAX;
            break;
        }
        capacity *= 2;
    }

    NC_ASSERT(capacity >= required);
    return capacity;
}

static uint32_t nc__renderer_align_u32(const uint32_t value, const uint32_t alignment) {
    if (alignment <= 1) {
        return value;
    }

    return (value + alignment - 1) / alignment * alignment;
}

static void nc__renderer_free_texture_file_data(nc__renderer_texture_file_data_t* data) {
    free(data->bytes);
    *data = (nc__renderer_texture_file_data_t){ 0 };
}

static void nc__renderer_wait_idle(nc_renderer_t* renderer) {
    if (!renderer->device) {
        return;
    }

    vkDeviceWaitIdle(renderer->device);
    renderer->frame_in_progress = false;
    renderer->frame_fence_pending = false;
}

static void nc__renderer_destroy_retired_transfer_buffers(nc_renderer_t* renderer) {
    for (uint32_t i = 0; i < renderer->retired_transfer_buffer_count; i++) {
        const nc__renderer_retired_buffer_t* retired = &renderer->retired_transfer_buffers[i];
        vmaDestroyBuffer(renderer->allocator, retired->buffer, retired->allocation);
    }
    renderer->retired_transfer_buffer_count = 0;
}

static void nc__renderer_retire_transfer_buffer(
    nc_renderer_t* renderer,
    const VkBuffer buffer,
    const VmaAllocation allocation
) {
    if (!buffer) {
        return;
    }

    if (renderer->retired_transfer_buffer_count == renderer->retired_transfer_buffer_capacity) {
        const uint32_t new_capacity = nc__renderer_next_capacity(
                renderer->retired_transfer_buffer_capacity,
                renderer->retired_transfer_buffer_count + 1,
                4);
        renderer->retired_transfer_buffers = realloc(
                renderer->retired_transfer_buffers,
                new_capacity * sizeof(*renderer->retired_transfer_buffers));
        renderer->retired_transfer_buffer_capacity = new_capacity;
    }

    renderer->retired_transfer_buffers[renderer->retired_transfer_buffer_count++] =
            (nc__renderer_retired_buffer_t){ buffer, allocation };
}

static bool nc__renderer_create_transfer_buffer(nc_renderer_t* renderer, const uint32_t capacity) {
    VmaAllocationInfo allocation_info;
    NC__CHECK_VK_RESULT(vmaCreateBuffer(
            renderer->allocator,
            &(VkBufferCreateInfo){
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = capacity,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            },
            &(VmaAllocationCreateInfo){
                .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
            },
            &renderer->transfer_buffer,
            &renderer->transfer_allocation,
            &allocation_info));
    renderer->mapped_transfer_buffer = allocation_info.pMappedData;
    renderer->transfer_capacity = capacity;
    return true;

error:
    return false;
}

static bool nc__renderer_grow_transfer_buffer(nc_renderer_t* renderer, const uint32_t required) {
    const uint32_t new_capacity = nc__renderer_next_capacity(
            renderer->transfer_capacity,
            required,
            NC__RENDERER_INITIAL_TRANSFER_CAPACITY);
    VkBuffer old_buffer = renderer->transfer_buffer;
    VmaAllocation old_allocation = renderer->transfer_allocation;
    void* old_mapping = renderer->mapped_transfer_buffer;

    renderer->transfer_buffer = VK_NULL_HANDLE;
    renderer->transfer_allocation = VK_NULL_HANDLE;
    renderer->mapped_transfer_buffer = NULL;

    if (!nc__renderer_create_transfer_buffer(renderer, new_capacity)) {
        renderer->transfer_buffer = old_buffer;
        renderer->transfer_allocation = old_allocation;
        renderer->mapped_transfer_buffer = old_mapping;
        return false;
    }

    if (old_mapping && renderer->transfer_size > 0) {
        memcpy(renderer->mapped_transfer_buffer, old_mapping, renderer->transfer_size);
        NC__CHECK_VK_RESULT(vmaFlushAllocation(renderer->allocator, old_allocation, 0, renderer->transfer_size));
    }

    if (renderer->frame_in_progress || renderer->frame_fence_pending) {
        nc__renderer_retire_transfer_buffer(renderer, old_buffer, old_allocation);
    } else if (old_buffer) {
        vmaDestroyBuffer(renderer->allocator, old_buffer, old_allocation);
    }

    return true;

error:
    if (renderer->transfer_buffer) {
        vmaDestroyBuffer(renderer->allocator, renderer->transfer_buffer, renderer->transfer_allocation);
    }
    renderer->transfer_buffer = old_buffer;
    renderer->transfer_allocation = old_allocation;
    renderer->mapped_transfer_buffer = old_mapping;
    return false;
}

static bool nc__renderer_reserve_transfer_bytes(
    nc_renderer_t* renderer,
    const uint32_t size,
    const uint32_t alignment,
    uint32_t* out_offset
) {
    NC_ASSERT(size);

    const uint32_t offset = nc__renderer_align_u32(renderer->transfer_size, alignment);
    const uint32_t required = offset + size;
    NC_ASSERT(required >= offset);

    if (required > renderer->transfer_capacity && !nc__renderer_grow_transfer_buffer(renderer, required)) {
        return false;
    }

    *out_offset = offset;
    renderer->transfer_size = required;
    return true;
}

static bool nc__renderer_physical_device_supports_extensions(const VkPhysicalDevice physical_device) {
    uint32_t extension_count = 0;
    VkExtensionProperties* extensions = NULL;
    NC__CHECK_VK_RESULT(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &extension_count, NULL));

    extensions = malloc(extension_count * sizeof(*extensions));
    NC__CHECK_VK_RESULT(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &extension_count, extensions));

    bool has_swapchain = false;
    for (uint32_t i = 0; i < extension_count; i++) {
        if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            has_swapchain = true;
            break;
        }
    }

    free(extensions);
    return has_swapchain;

error:
    free(extensions);
    return false;
}

static bool nc__renderer_find_queue_family(
    const nc_renderer_t* renderer,
    const VkPhysicalDevice physical_device,
    uint32_t* out_queue_family_index
) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);

    VkQueueFamilyProperties* queue_families = malloc(queue_family_count * sizeof(*queue_families));
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; i++) {
        VkBool32 present_supported = VK_FALSE;
        NC__CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceSupportKHR(
                physical_device,
                i,
                renderer->surface,
                &present_supported));

        const VkQueueFlags required_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if (queue_families[i].queueCount > 0 &&
                present_supported &&
                (queue_families[i].queueFlags & required_flags) == required_flags) {
            *out_queue_family_index = i;
            free(queue_families);
            return true;
        }
    }

    free(queue_families);
    return false;

error:
    free(queue_families);
    return false;
}

static bool nc__renderer_select_physical_device(nc_renderer_t* renderer) {
    uint32_t physical_device_count = 0;
    VkPhysicalDevice* physical_devices = NULL;
    NC__CHECK_VK_RESULT(vkEnumeratePhysicalDevices(renderer->instance, &physical_device_count, NULL));
    NC_CHECK_RESULT(physical_device_count > 0, "No Vulkan physical devices were found.");

    physical_devices = malloc(physical_device_count * sizeof(*physical_devices));
    NC__CHECK_VK_RESULT(vkEnumeratePhysicalDevices(renderer->instance, &physical_device_count, physical_devices));

    for (uint32_t i = 0; i < physical_device_count; i++) {
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);

#if NC__RENDERER_ASTC_TEXTURES
        if (!features.textureCompressionASTC_LDR) {
            continue;
        }
#endif

        uint32_t queue_family_index = UINT32_MAX;
        if (!nc__renderer_physical_device_supports_extensions(physical_devices[i]) ||
                !nc__renderer_find_queue_family(renderer, physical_devices[i], &queue_family_index)) {
            continue;
        }

        renderer->physical_device = physical_devices[i];
        renderer->queue_family_index = queue_family_index;
        vkGetPhysicalDeviceProperties(renderer->physical_device, &renderer->physical_device_properties);
        free(physical_devices);
        return true;
    }

    NC_SET_ERROR("No suitable Vulkan physical device was found.");

error:
    free(physical_devices);
    return false;
}

static bool nc__renderer_create_instance(nc_renderer_t* renderer) {
    uint32_t extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    NC_CHECK_SDL_RESULT(extensions);

    NC__CHECK_VK_RESULT(vkCreateInstance(
            &(VkInstanceCreateInfo){
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo){
                    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                    .pApplicationName = NC_PRODUCT_NAME,
                    .applicationVersion = NC_VULKAN_APPLICATION_VERSION,
                    .apiVersion = NC__RENDERER_VK_API_VERSION,
                },
                .enabledExtensionCount = extension_count,
                .ppEnabledExtensionNames = extensions,
            },
            NULL,
            &renderer->instance));
    volkLoadInstanceOnly(renderer->instance);
    return true;

error:
    return false;
}

static bool nc__renderer_create_device(nc_renderer_t* renderer) {
    const float queue_priority = 1.0f;
    VkPhysicalDeviceFeatures enabled_features = { 0 };
    const VmaVulkanFunctions vma_functions = {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
#if NC__RENDERER_ASTC_TEXTURES
    enabled_features.textureCompressionASTC_LDR = VK_TRUE;
#endif

    NC__CHECK_VK_RESULT(vkCreateDevice(
            renderer->physical_device,
            &(VkDeviceCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = renderer->queue_family_index,
                    .queueCount = 1,
                    .pQueuePriorities = &queue_priority,
                },
                .enabledExtensionCount = 1,
                .ppEnabledExtensionNames = (const char* const[]){ VK_KHR_SWAPCHAIN_EXTENSION_NAME },
                .pEnabledFeatures = &enabled_features,
            },
            NULL,
            &renderer->device));
    volkLoadDevice(renderer->device);
    vkGetDeviceQueue(renderer->device, renderer->queue_family_index, 0, &renderer->queue);

    NC__CHECK_VK_RESULT(vmaCreateAllocator(
            &(VmaAllocatorCreateInfo){
                .physicalDevice = renderer->physical_device,
                .device = renderer->device,
                .instance = renderer->instance,
                .vulkanApiVersion = NC__RENDERER_VK_API_VERSION,
                .pVulkanFunctions = &vma_functions,
            },
            &renderer->allocator));
    return true;

error:
    return false;
}

static bool nc__renderer_create_window(nc_renderer_t* renderer, const nc_renderer_create_info_t* info) {
    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

#ifdef ANDROID
    window_flags |= SDL_WINDOW_FULLSCREEN;
#else
    bool exclusive = false;
    switch (info->video_mode) {
        case NC_VIDEO_MODE_WINDOW:
            break;
        case NC_VIDEO_MODE_BORDERLESS:
            window_flags |= SDL_WINDOW_BORDERLESS;
            break;
        case NC_VIDEO_MODE_EXCLUSIVE_FULLSCREEN:
            exclusive = true;
            window_flags |= SDL_WINDOW_FULLSCREEN;
            break;
        case NC_VIDEO_MODE_FULLSCREEN:
            window_flags |= SDL_WINDOW_FULLSCREEN;
            break;
    }
#endif

    renderer->window = SDL_CreateWindow(info->window_title, info->window_width, info->window_height, window_flags);
    NC_CHECK_SDL_RESULT(renderer->window);

#ifndef ANDROID
    if (exclusive) {
        SDL_DisplayMode display_mode;
        bool sdl_result = SDL_GetClosestFullscreenDisplayMode(
                SDL_GetDisplayForWindow(renderer->window),
                info->window_width,
                info->window_height,
                (float)info->refresh_rate,
                true,
                &display_mode);
        if (!sdl_result) {
            SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_GetClosestFullscreenDisplayMode() failed: %s",
                    SDL_GetError());
            SDL_ClearError();
        } else if (!SDL_SetWindowFullscreenMode(renderer->window, &display_mode)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetWindowFullscreenMode() failed: %s", SDL_GetError());
            SDL_ClearError();
        }
    }
#endif

    int width;
    int height;
    bool sdl_result = SDL_GetWindowSize(renderer->window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);
    renderer->window_size.x = (uint16_t)width;
    renderer->window_size.y = (uint16_t)height;

    sdl_result = SDL_GetWindowSizeInPixels(renderer->window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);
    renderer->viewport.x = (uint16_t)width;
    renderer->viewport.y = (uint16_t)height;
    return true;

error:
    return false;
}

static bool nc__renderer_create_surface(nc_renderer_t* renderer) {
    if (renderer->surface) {
        SDL_Vulkan_DestroySurface(renderer->instance, renderer->surface, NULL);
        renderer->surface = VK_NULL_HANDLE;
    }

    const bool sdl_result = SDL_Vulkan_CreateSurface(renderer->window, renderer->instance, NULL, &renderer->surface);
    NC_CHECK_SDL_RESULT(sdl_result);
    return true;

error:
    return false;
}

static bool nc__renderer_choose_swapchain_format(
    const VkSurfaceFormatKHR* formats,
    const uint32_t format_count,
    VkSurfaceFormatKHR* out_format
) {
    NC_CHECK_RESULT(format_count > 0, "The Vulkan surface has no supported formats.");

    if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        *out_format = (VkSurfaceFormatKHR){
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
        return true;
    }

    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *out_format = formats[i];
            return true;
        }
    }

    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *out_format = formats[i];
            return true;
        }
    }

    *out_format = formats[0];
    return true;

error:
    return false;
}

static bool nc__renderer_get_swapchain_extent(
    const nc_renderer_t* renderer,
    const VkSurfaceCapabilitiesKHR* capabilities,
    VkExtent2D* out_extent
) {
    if (capabilities->currentExtent.width != UINT32_MAX) {
        *out_extent = capabilities->currentExtent;
        return out_extent->width > 0 && out_extent->height > 0;
    }

    int width;
    int height;
    const bool sdl_result = SDL_GetWindowSizeInPixels(renderer->window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);

    if (width <= 0 || height <= 0) {
        return false;
    }

    out_extent->width = vkm_clamp(
            (uint32_t)width,
            capabilities->minImageExtent.width,
            capabilities->maxImageExtent.width);
    out_extent->height = vkm_clamp(
            (uint32_t)height,
            capabilities->minImageExtent.height,
            capabilities->maxImageExtent.height);
    return true;

error:
    return false;
}

static bool nc__renderer_create_render_pass(nc_renderer_t* renderer) {
    const VkAttachmentDescription attachments[] = {
        {
            .format = renderer->swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        {
            .format = NC__RENDERER_DEPTH_FORMAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    const VkAttachmentReference color_attachment = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference depth_attachment = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthStencilAttachment = &depth_attachment,
    };
    const VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    NC__CHECK_VK_RESULT(vkCreateRenderPass(
            renderer->device,
            &(VkRenderPassCreateInfo){
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = NC_COUNTOF(attachments),
                .pAttachments = attachments,
                .subpassCount = 1,
                .pSubpasses = &subpass,
                .dependencyCount = 1,
                .pDependencies = &dependency,
            },
            NULL,
            &renderer->render_pass));
    return true;

error:
    return false;
}

static void nc__renderer_destroy_depth_image(nc_renderer_t* renderer) {
    vkDestroyImageView(renderer->device, renderer->depth_image_view, NULL);
    renderer->depth_image_view = VK_NULL_HANDLE;

    if (renderer->depth_image) {
        vmaDestroyImage(renderer->allocator, renderer->depth_image, renderer->depth_allocation);
        renderer->depth_image = VK_NULL_HANDLE;
        renderer->depth_allocation = VK_NULL_HANDLE;
    }
}

static bool nc__renderer_create_depth_image(nc_renderer_t* renderer) {
    nc__renderer_destroy_depth_image(renderer);

    NC__CHECK_VK_RESULT(vmaCreateImage(
            renderer->allocator,
            &(VkImageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = NC__RENDERER_DEPTH_FORMAT,
                .extent = { renderer->swapchain_extent.width, renderer->swapchain_extent.height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
            &(VmaAllocationCreateInfo){
                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
            },
            &renderer->depth_image,
            &renderer->depth_allocation,
            NULL));

    NC__CHECK_VK_RESULT(vkCreateImageView(
            renderer->device,
            &(VkImageViewCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = renderer->depth_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = NC__RENDERER_DEPTH_FORMAT,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
            NULL,
            &renderer->depth_image_view));
    return true;

error:
    nc__renderer_destroy_depth_image(renderer);
    return false;
}

static void nc__renderer_destroy_framebuffers(nc_renderer_t* renderer) {
    if (renderer->framebuffers) {
        for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
            vkDestroyFramebuffer(renderer->device, renderer->framebuffers[i], NULL);
        }
        free(renderer->framebuffers);
        renderer->framebuffers = NULL;
    }
}

static bool nc__renderer_create_framebuffers(nc_renderer_t* renderer) {
    renderer->framebuffers = calloc(renderer->swapchain_image_count, sizeof(*renderer->framebuffers));
    for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
        const VkImageView attachments[] = {
            renderer->swapchain_image_views[i],
            renderer->depth_image_view,
        };
        NC__CHECK_VK_RESULT(vkCreateFramebuffer(
                renderer->device,
                &(VkFramebufferCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                    .renderPass = renderer->render_pass,
                    .attachmentCount = NC_COUNTOF(attachments),
                    .pAttachments = attachments,
                    .width = renderer->swapchain_extent.width,
                    .height = renderer->swapchain_extent.height,
                    .layers = 1,
                },
                NULL,
                &renderer->framebuffers[i]));
    }
    return true;

error:
    nc__renderer_destroy_framebuffers(renderer);
    return false;
}

static void nc__renderer_destroy_present_semaphores(nc_renderer_t* renderer) {
    if (renderer->render_finished_semaphores) {
        for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
            vkDestroySemaphore(renderer->device, renderer->render_finished_semaphores[i], NULL);
        }
        free(renderer->render_finished_semaphores);
        renderer->render_finished_semaphores = NULL;
    }
}

static bool nc__renderer_create_present_semaphores(nc_renderer_t* renderer) {
    renderer->render_finished_semaphores = calloc(
            renderer->swapchain_image_count,
            sizeof(*renderer->render_finished_semaphores));
    NC_CHECK_RESULT(renderer->render_finished_semaphores, "Failed to allocate Vulkan present semaphores.");
    for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
        NC__CHECK_VK_RESULT(vkCreateSemaphore(
                renderer->device,
                &(VkSemaphoreCreateInfo){ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO },
                NULL,
                &renderer->render_finished_semaphores[i]));
    }
    return true;

error:
    nc__renderer_destroy_present_semaphores(renderer);
    return false;
}

static void nc__renderer_destroy_swapchain(nc_renderer_t* renderer) {
    nc__renderer_destroy_framebuffers(renderer);
    nc__renderer_destroy_depth_image(renderer);
    nc__renderer_destroy_present_semaphores(renderer);

    if (renderer->swapchain_image_views) {
        for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
            vkDestroyImageView(renderer->device, renderer->swapchain_image_views[i], NULL);
        }
        free(renderer->swapchain_image_views);
        renderer->swapchain_image_views = NULL;
    }

    free(renderer->swapchain_images);
    renderer->swapchain_images = NULL;
    renderer->swapchain_image_count = 0;

    vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
    renderer->swapchain = VK_NULL_HANDLE;
}

static bool nc__renderer_create_swapchain(nc_renderer_t* renderer) {
    VkSurfaceFormatKHR* formats = NULL;
    VkSurfaceCapabilitiesKHR capabilities;
    NC__CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            renderer->physical_device,
            renderer->surface,
            &capabilities));

    uint32_t format_count = 0;
    NC__CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(
            renderer->physical_device,
            renderer->surface,
            &format_count,
            NULL));
    formats = malloc(format_count * sizeof(*formats));
    NC__CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(
            renderer->physical_device,
            renderer->surface,
            &format_count,
            formats));

    VkSurfaceFormatKHR chosen_format;
    if (!nc__renderer_choose_swapchain_format(formats, format_count, &chosen_format)) {
        goto error;
    }

    VkExtent2D extent;
    if (!nc__renderer_get_swapchain_extent(renderer, &capabilities, &extent)) {
        free(formats);
        return true;
    }

    if (renderer->render_pass && renderer->swapchain_format != chosen_format.format) {
        NC_SET_ERROR("The swapchain format changed after renderer initialization.");
        goto error;
    }

    // Ask for triple buffering.
    const uint32_t image_count = vkm_clamp(3, capabilities.minImageCount, capabilities.maxImageCount);
    NC__CHECK_VK_RESULT(vkCreateSwapchainKHR(
            renderer->device,
            &(VkSwapchainCreateInfoKHR){
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .surface = renderer->surface,
                .minImageCount = image_count,
                .imageFormat = chosen_format.format,
                .imageColorSpace = chosen_format.colorSpace,
                .imageExtent = extent,
                .imageArrayLayers = 1,
                .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .preTransform = capabilities.currentTransform,
                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                .clipped = VK_TRUE,
            },
            NULL,
            &renderer->swapchain));

    renderer->swapchain_format = chosen_format.format;
    renderer->swapchain_color_space = chosen_format.colorSpace;
    renderer->swapchain_extent = extent;
    renderer->viewport.x = (uint16_t)extent.width;
    renderer->viewport.y = (uint16_t)extent.height;

    NC__CHECK_VK_RESULT(vkGetSwapchainImagesKHR(
            renderer->device,
            renderer->swapchain,
            &renderer->swapchain_image_count,
            NULL));
    renderer->swapchain_images = malloc(renderer->swapchain_image_count * sizeof(*renderer->swapchain_images));
    NC__CHECK_VK_RESULT(vkGetSwapchainImagesKHR(
            renderer->device,
            renderer->swapchain,
            &renderer->swapchain_image_count,
            renderer->swapchain_images));

    renderer->swapchain_image_views = calloc(renderer->swapchain_image_count, sizeof(*renderer->swapchain_image_views));
    for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
        NC__CHECK_VK_RESULT(vkCreateImageView(
                renderer->device,
                &(VkImageViewCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image = renderer->swapchain_images[i],
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = renderer->swapchain_format,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                    },
                },
                NULL,
                &renderer->swapchain_image_views[i]));
    }

    if (!renderer->render_pass && !nc__renderer_create_render_pass(renderer)) {
        goto error;
    }
    if (!nc__renderer_create_present_semaphores(renderer) ||
            !nc__renderer_create_depth_image(renderer) ||
            !nc__renderer_create_framebuffers(renderer)) {
        goto error;
    }

    free(formats);
    renderer->swapchain_dirty = false;
    renderer->surface_dirty = false;
    return true;

error:
    free(formats);
    nc__renderer_destroy_swapchain(renderer);
    return false;
}

static bool nc__renderer_recreate_swapchain(nc_renderer_t* renderer) {
    nc__renderer_wait_idle(renderer);

    if (renderer->surface_dirty && !nc__renderer_create_surface(renderer)) {
        return false;
    }

    nc__renderer_destroy_swapchain(renderer);
    return nc__renderer_create_swapchain(renderer);
}

static bool nc__renderer_create_descriptor_pool(nc_renderer_t* renderer) {
    const VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
        },
    };

    NC__CHECK_VK_RESULT(vkCreateDescriptorPool(
            renderer->device,
            &(VkDescriptorPoolCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = 0,
                .maxSets = NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
                .poolSizeCount = NC_COUNTOF(pool_sizes),
                .pPoolSizes = pool_sizes,
            },
            NULL,
            &renderer->frame_descriptor_pool));
    return true;

error:
    return false;
}

static bool nc__renderer_create_descriptor_set_layouts(nc_renderer_t* renderer) {
    const VkDescriptorSetLayoutBinding set0_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const VkDescriptorSetLayoutBinding set1_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const VkDescriptorSetLayoutBinding set2_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const VkDescriptorSetLayoutBinding set3_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const struct {
        const VkDescriptorSetLayoutBinding* bindings;
        uint32_t binding_count;
    } layouts[] = {
        { set0_bindings, NC_COUNTOF(set0_bindings) },
        { set1_bindings, NC_COUNTOF(set1_bindings) },
        { set2_bindings, NC_COUNTOF(set2_bindings) },
        { set3_bindings, NC_COUNTOF(set3_bindings) },
    };

    for (uint32_t i = 0; i < NC_COUNTOF(renderer->descriptor_set_layouts); i++) {
        NC__CHECK_VK_RESULT(vkCreateDescriptorSetLayout(
                renderer->device,
                &(VkDescriptorSetLayoutCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = layouts[i].binding_count,
                    .pBindings = layouts[i].bindings,
                },
                NULL,
                &renderer->descriptor_set_layouts[i]));
    }

    NC__CHECK_VK_RESULT(vkCreatePipelineLayout(
            renderer->device,
            &(VkPipelineLayoutCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = NC_COUNTOF(renderer->descriptor_set_layouts),
                .pSetLayouts = renderer->descriptor_set_layouts,
            },
            NULL,
            &renderer->pipeline_layout));
    return true;

error:
    return false;
}

static void nc__renderer_destroy_descriptor_state(nc_renderer_t* renderer) {
    vkDestroyDescriptorPool(renderer->device, renderer->frame_descriptor_pool, NULL);
    renderer->frame_descriptor_pool = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(renderer->device, renderer->pipeline_layout, NULL);
    renderer->pipeline_layout = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < NC_COUNTOF(renderer->descriptor_set_layouts); i++) {
        vkDestroyDescriptorSetLayout(renderer->device, renderer->descriptor_set_layouts[i], NULL);
        renderer->descriptor_set_layouts[i] = VK_NULL_HANDLE;
    }
}

static VkShaderModule nc__renderer_load_shader(const nc_renderer_t* renderer, const char* path) {
    size_t code_size = 0;
    void* code = SDL_LoadFile(path, &code_size);
    NC_CHECK_SDL_RESULT(code);
    NC_CHECK_RESULT(code_size % sizeof(uint32_t) == 0, "Shader bytecode size is not a multiple of 4: %s", path);

    VkShaderModule shader_module;
    NC__CHECK_VK_RESULT(vkCreateShaderModule(
            renderer->device,
            &(VkShaderModuleCreateInfo){
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code_size,
                .pCode = code,
            },
            NULL,
            &shader_module));

    SDL_free(code);
    return shader_module;

error:
    SDL_free(code);
    return VK_NULL_HANDLE;
}

static bool nc__renderer_create_graphics_pipeline(
    const nc_renderer_t* renderer,
    const char* vertex_shader_path,
    const char* fragment_shader_path,
    const VkPipelineVertexInputStateCreateInfo* vertex_input_state,
    const VkPipelineRasterizationStateCreateInfo* rasterization_state,
    const VkPipelineDepthStencilStateCreateInfo* depth_stencil_state,
    const VkPipelineColorBlendAttachmentState* color_blend_attachment,
    VkPipeline* out_pipeline
) {
    VkShaderModule vertex_shader = nc__renderer_load_shader(renderer, vertex_shader_path);
    VkShaderModule fragment_shader = nc__renderer_load_shader(renderer, fragment_shader_path);
    NC_CHECK_RESULT(vertex_shader && fragment_shader, "Failed to load Vulkan shaders.");

    const VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = "main",
        },
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = color_blend_attachment,
    };
    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    const VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = NC_COUNTOF(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    NC__CHECK_VK_RESULT(vkCreateGraphicsPipelines(
            renderer->device,
            VK_NULL_HANDLE,
            1,
            &(VkGraphicsPipelineCreateInfo){
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = NC_COUNTOF(shader_stages),
                .pStages = shader_stages,
                .pVertexInputState = vertex_input_state,
                .pInputAssemblyState = &input_assembly_state,
                .pViewportState = &viewport_state,
                .pRasterizationState = rasterization_state,
                .pMultisampleState = &multisample_state,
                .pDepthStencilState = depth_stencil_state,
                .pColorBlendState = &color_blend_state,
                .pDynamicState = &dynamic_state,
                .layout = renderer->pipeline_layout,
                .renderPass = renderer->render_pass,
                .subpass = 0,
            },
            NULL,
            out_pipeline));

    vkDestroyShaderModule(renderer->device, fragment_shader, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader, NULL);
    return true;

error:
    vkDestroyShaderModule(renderer->device, fragment_shader, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader, NULL);
    return false;
}

static bool nc__renderer_create_pipelines(nc_renderer_t* renderer) {
    const VkPipelineVertexInputStateCreateInfo no_vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineRasterizationStateCreateInfo raster_back_cull = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineRasterizationStateCreateInfo raster_no_cull = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineDepthStencilStateCreateInfo depth_enabled_write = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const VkPipelineDepthStencilStateCreateInfo depth_enabled_no_write = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const VkPipelineDepthStencilStateCreateInfo depth_disabled = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
    };
    const VkPipelineColorBlendAttachmentState no_blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendAttachmentState alpha_blend = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendAttachmentState subtract_blend = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_SUBTRACT,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/chunk-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/chunk-frag.spv",
            &no_vertex_input,
            &raster_back_cull,
            &depth_enabled_write,
            &no_blend,
            &renderer->chunk_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/block-highlight-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/block-highlight-outline-frag.spv",
            &no_vertex_input,
            &raster_back_cull,
            &depth_enabled_no_write,
            &alpha_blend,
            &renderer->outline_block_highlight_pipeline) ||
            !nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/block-highlight-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/block-highlight-vignette-frag.spv",
            &no_vertex_input,
            &raster_back_cull,
            &depth_enabled_no_write,
            &alpha_blend,
            &renderer->vignette_block_highlight_pipeline) ||
            !nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/block-highlight-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/block-highlight-plasma-frag.spv",
            &no_vertex_input,
            &raster_back_cull,
            &depth_enabled_no_write,
            &alpha_blend,
            &renderer->plasma_block_highlight_pipeline)) {
        return false;
    }

    const VkVertexInputBindingDescription gui_binding = {
        .binding = 0,
        .stride = sizeof(vkm_vec2) + sizeof(vkm_vec2) + sizeof(vkm_ubvec4),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription gui_attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = sizeof(vkm_vec2),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = sizeof(vkm_vec2) + sizeof(vkm_vec2),
        },
    };
    const VkPipelineVertexInputStateCreateInfo gui_vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &gui_binding,
        .vertexAttributeDescriptionCount = NC_COUNTOF(gui_attributes),
        .pVertexAttributeDescriptions = gui_attributes,
    };
    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/gui-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/gui-frag.spv",
            &gui_vertex_input,
            &raster_no_cull,
            &depth_disabled,
            &alpha_blend,
            &renderer->gui_pipeline)) {
        return false;
    }

    return nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/procedural-overlay-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/procedural-overlay-invert-frag.spv",
            &no_vertex_input,
            &raster_no_cull,
            &depth_disabled,
            &subtract_blend,
            &renderer->procedural_overlay_invert_pipeline) &&
            nc__renderer_create_graphics_pipeline(
            renderer,
            NC__RENDERER_ASSETS_BASE_PATH "shaders/procedural-overlay-vert.spv",
            NC__RENDERER_ASSETS_BASE_PATH "shaders/procedural-overlay-stick-frag.spv",
            &no_vertex_input,
            &raster_no_cull,
            &depth_disabled,
            &no_blend,
            &renderer->procedural_overlay_stick_pipeline);
}

static void nc__renderer_destroy_pipelines(nc_renderer_t* renderer) {
    vkDestroyPipeline(renderer->device, renderer->procedural_overlay_stick_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->procedural_overlay_invert_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->gui_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->chunk_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->plasma_block_highlight_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->vignette_block_highlight_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->outline_block_highlight_pipeline, NULL);

    renderer->procedural_overlay_stick_pipeline = VK_NULL_HANDLE;
    renderer->procedural_overlay_invert_pipeline = VK_NULL_HANDLE;
    renderer->gui_pipeline = VK_NULL_HANDLE;
    renderer->chunk_pipeline = VK_NULL_HANDLE;
    renderer->plasma_block_highlight_pipeline = VK_NULL_HANDLE;
    renderer->vignette_block_highlight_pipeline = VK_NULL_HANDLE;
    renderer->outline_block_highlight_pipeline = VK_NULL_HANDLE;
}

static bool nc__renderer_create_sampler(const nc_renderer_t* renderer, VkSampler* sampler) {
    NC__CHECK_VK_RESULT(vkCreateSampler(
            renderer->device,
            &(VkSamplerCreateInfo){
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .maxLod = VK_LOD_CLAMP_NONE,
            },
            NULL,
            sampler));
    return true;

error:
    return false;
}

static bool nc__renderer_create_frame_resources(nc_renderer_t* renderer) {
    NC__CHECK_VK_RESULT(vkCreateCommandPool(
            renderer->device,
            &(VkCommandPoolCreateInfo){
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = renderer->queue_family_index,
            },
            NULL,
            &renderer->command_pool));
    NC__CHECK_VK_RESULT(vkAllocateCommandBuffers(
            renderer->device,
            &(VkCommandBufferAllocateInfo){
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = renderer->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            },
            &renderer->frame_command_buffer));
    NC__CHECK_VK_RESULT(vkCreateFence(
            renderer->device,
            &(VkFenceCreateInfo){
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            },
            NULL,
            &renderer->frame_fence));
    NC__CHECK_VK_RESULT(vkCreateSemaphore(
            renderer->device,
            &(VkSemaphoreCreateInfo){ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO },
            NULL,
            &renderer->image_available_semaphore));
    return true;

error:
    return false;
}

static bool nc__renderer_allocate_descriptor_set(
    nc_renderer_t* renderer,
    const uint32_t set_index,
    VkDescriptorSet* out_descriptor_set
) {
    NC_ASSERT(set_index < NC_COUNTOF(renderer->descriptor_set_layouts));
    if (renderer->frame_descriptor_set_count >= NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS) {
        NC_SET_ERROR("The per-frame Vulkan descriptor pool is exhausted.");
        return false;
    }

    const VkResult result = vkAllocateDescriptorSets(
            renderer->device,
            &(VkDescriptorSetAllocateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = renderer->frame_descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &renderer->descriptor_set_layouts[set_index],
            },
            out_descriptor_set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        NC_SET_ERROR("The per-frame Vulkan descriptor pool is exhausted.");
        return false;
    }
    NC__CHECK_VK_RESULT(result);

    renderer->frame_descriptor_set_count++;
    return true;

error:
    return false;
}

static bool nc__renderer_write_uniform_descriptor_set(
    nc_renderer_t* renderer,
    const uint32_t set_index,
    const void* data,
    const uint32_t size,
    VkDescriptorSet* out_descriptor_set
) {
    const uint32_t alignment = (uint32_t)renderer->physical_device_properties.limits.minUniformBufferOffsetAlignment;
    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, alignment ? alignment : 1, &offset)) {
        return false;
    }
    memcpy((uint8_t*)renderer->mapped_transfer_buffer + offset, data, size);

    VkDescriptorSet descriptor_set;
    if (!nc__renderer_allocate_descriptor_set(renderer, set_index, &descriptor_set)) {
        return false;
    }

    const VkDescriptorBufferInfo buffer_info = {
        .buffer = renderer->transfer_buffer,
        .offset = offset,
        .range = size,
    };
    vkUpdateDescriptorSets(
            renderer->device,
            1,
            &(VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            },
            0,
            NULL);
    *out_descriptor_set = descriptor_set;
    return true;
}

static bool nc__renderer_write_storage_descriptor_set(
    nc_renderer_t* renderer,
    const nc_renderer_buffer_t* buffer,
    VkDescriptorSet* out_descriptor_set
) {
    VkDescriptorSet descriptor_set;
    if (!nc__renderer_allocate_descriptor_set(renderer, 0, &descriptor_set)) {
        return false;
    }

    const VkDescriptorBufferInfo buffer_info = {
        .buffer = buffer->buffer,
        .range = buffer->capacity,
    };
    vkUpdateDescriptorSets(
            renderer->device,
            1,
            &(VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_info,
            },
            0,
            NULL);
    *out_descriptor_set = descriptor_set;
    return true;
}

static bool nc__renderer_write_texture_descriptor_set(
    nc_renderer_t* renderer,
    const nc_renderer_texture_t* texture,
    const nc_renderer_buffer_t* storage_buffer,
    const VkSampler sampler,
    VkDescriptorSet* out_descriptor_set
) {
    VkDescriptorSet descriptor_set;
    if (!nc__renderer_allocate_descriptor_set(renderer, 2, &descriptor_set)) {
        return false;
    }

    if (!storage_buffer) {
        storage_buffer = renderer->dummy_storage_buffer;
    }

    const VkDescriptorImageInfo image_info = {
        .sampler = sampler,
        .imageView = texture->image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkDescriptorBufferInfo buffer_info = {
        .buffer = storage_buffer->buffer,
        .range = storage_buffer->capacity,
    };
    const VkWriteDescriptorSet writes[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer_info,
        },
    };
    vkUpdateDescriptorSets(renderer->device, NC_COUNTOF(writes), writes, 0, NULL);
    *out_descriptor_set = descriptor_set;
    return true;
}

static void nc__renderer_set_viewport_and_scissor(const nc_renderer_t* renderer, const SDL_Rect* scissor_rect) {
    vkCmdSetViewport(
            renderer->frame_command_buffer,
            0,
            1,
            &(VkViewport){
                .x = 0.0f,
                .y = 0.0f,
                .width = (float)renderer->viewport.x,
                .height = (float)renderer->viewport.y,
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            });

    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = { renderer->viewport.x, renderer->viewport.y },
    };
    if (scissor_rect) {
        scissor.offset.x = scissor_rect->x;
        scissor.offset.y = scissor_rect->y;
        scissor.extent.width = (uint32_t)vkm_max(scissor_rect->w, 0);
        scissor.extent.height = (uint32_t)vkm_max(scissor_rect->h, 0);
    }
    vkCmdSetScissor(renderer->frame_command_buffer, 0, 1, &scissor);
}

static void nc__renderer_cmd_transition_texture(
    const nc_renderer_t* renderer,
    nc_renderer_texture_t* texture,
    const VkImageLayout new_layout
) {
    if (texture->layout == new_layout) {
        return;
    }

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags src_access = 0;
    VkAccessFlags dst_access = 0;

    switch (texture->layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            src_access = VK_ACCESS_SHADER_READ_BIT;
            break;
        default:
            break;
    }

    switch (new_layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dst_access = VK_ACCESS_SHADER_READ_BIT;
            break;
        default:
            NC_ASSERT(false);
            break;
    }

    vkCmdPipelineBarrier(
            renderer->frame_command_buffer,
            src_stage,
            dst_stage,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &(VkImageMemoryBarrier){
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = src_access,
                .dstAccessMask = dst_access,
                .oldLayout = texture->layout,
                .newLayout = new_layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = texture->image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = texture->layer_count,
                },
            });
    texture->layout = new_layout;
}

static void nc__renderer_cmd_buffer_upload_barrier(
    const nc_renderer_t* renderer,
    const nc_renderer_buffer_t* buffer
) {
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    VkAccessFlags dst_access = VK_ACCESS_SHADER_READ_BIT;
    if (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        dst_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        dst_access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    } else if (buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        dst_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        dst_access = VK_ACCESS_INDEX_READ_BIT;
    }

    vkCmdPipelineBarrier(
            renderer->frame_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            dst_stage,
            0,
            0,
            NULL,
            1,
            &(VkBufferMemoryBarrier){
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = dst_access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer->buffer,
                .offset = 0,
                .size = buffer->capacity,
            },
            0,
            NULL);
}

static bool nc__renderer_flush_uploads(nc_renderer_t* renderer) {
    if (!renderer->uploads_dirty || renderer->upload_count == 0) {
        return true;
    }

    NC_ASSERT(renderer->frame_command_buffer);

    for (uint32_t i = 0; i < renderer->upload_count; i++) {
        nc__renderer_upload_op_t* op = &renderer->upload_ops[i];
        if (op->kind == NC__RENDERER_UPLOAD_BUFFER) {
            vkCmdCopyBuffer(
                    renderer->frame_command_buffer,
                    renderer->transfer_buffer,
                    op->buffer->buffer,
                    1,
                    &(VkBufferCopy){
                        .srcOffset = op->source_offset,
                        .dstOffset = 0,
                        .size = op->size,
                    });
            nc__renderer_cmd_buffer_upload_barrier(renderer, op->buffer);
        } else {
            nc_renderer_texture_t* texture = op->texture.texture;
            nc__renderer_cmd_transition_texture(renderer, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(
                    renderer->frame_command_buffer,
                    renderer->transfer_buffer,
                    texture->image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &(VkBufferImageCopy){
                        .bufferOffset = op->source_offset,
                        .imageSubresource = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel = 0,
                            .baseArrayLayer = op->texture.layer,
                            .layerCount = 1,
                        },
                        .imageExtent = { (uint32_t)texture->width, (uint32_t)texture->height, 1 },
                    });
            nc__renderer_cmd_transition_texture(renderer, texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    renderer->upload_count = 0;
    renderer->uploads_dirty = false;
    return true;
}

#if NC__RENDERER_ASTC_TEXTURES
static uint32_t nc__renderer_read_u24(const uint8_t bytes[3]) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16);
}

static bool nc__renderer_load_texture_file(const char* path, nc__renderer_texture_file_data_t* out_texture) {
    size_t file_size = 0;
    uint8_t* file_bytes = SDL_LoadFile(path, &file_size);
    NC_CHECK_SDL_RESULT(file_bytes);

    if (file_size <= sizeof(nc__renderer_astc_header_t)) {
        NC_SET_ERROR("Invalid ASTC texture file (smaller than the header): %s", path);
        goto error;
    }

    const nc__renderer_astc_header_t* header = (const nc__renderer_astc_header_t*)file_bytes;
    uint32_t magic = 0;
    memcpy(&magic, header->magic, sizeof(magic));
    if (magic != 0x5ca1ab13u) {
        NC_SET_ERROR("Invalid ASTC texture header (wrong magic): %s", path);
        goto error;
    }
    if (header->block_x != 4 || header->block_y != 4 || header->block_z != 1) {
        NC_SET_ERROR(
                "Unsupported ASTC block size (%ux%ux%u, must be 4x4x1): %s",
                header->block_x,
                header->block_y,
                header->block_z,
                path);
        goto error;
    }
    if (nc__renderer_read_u24(header->dim_z) != 1) {
        NC_SET_ERROR("The ASTC depth is not 1: %s", path);
        goto error;
    }

    const uint32_t x = nc__renderer_read_u24(header->dim_x);
    const uint32_t y = nc__renderer_read_u24(header->dim_y);
    if (x > INT16_MAX || y > INT16_MAX) {
        NC_SET_ERROR("Texture %s is %ux%u, the max dimensions are %ix%i.", path, x, y, INT16_MAX, INT16_MAX);
        goto error;
    }

    out_texture->size = (uint32_t)(file_size - sizeof(*header));
    out_texture->bytes = malloc(out_texture->size);
    memcpy(out_texture->bytes, file_bytes + sizeof(*header), out_texture->size);
    out_texture->width = (int16_t)x;
    out_texture->height = (int16_t)y;
    out_texture->format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

    SDL_free(file_bytes);
    return true;

error:
    SDL_free(file_bytes);
    return false;
}
#else
static bool nc__renderer_load_texture_file(const char* path, nc__renderer_texture_file_data_t* out_texture) {
    SDL_Surface* surface = SDL_LoadPNG(path);
    NC_CHECK_SDL_RESULT(surface);

    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        NC_SET_ERROR(
                "Surface format is %s, expected SDL_PIXELFORMAT_RGBA32.",
                SDL_GetPixelFormatName(surface->format));
        goto error;
    }

    if (surface->w > INT16_MAX || surface->h > INT16_MAX) {
        NC_SET_ERROR(
                "Texture %s is %ix%i, the max dimensions are %ix%i.",
                path,
                surface->w,
                surface->h,
                INT16_MAX,
                INT16_MAX);
        goto error;
    }

    *out_texture = (nc__renderer_texture_file_data_t){
        .size = (uint32_t)surface->w * (uint32_t)surface->h * 4,
        .width = (int16_t)surface->w,
        .height = (int16_t)surface->h,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    };
    out_texture->bytes = memcpy(malloc(out_texture->size), surface->pixels, out_texture->size);

    SDL_DestroySurface(surface);
    return true;

error:
    SDL_DestroySurface(surface);
    return false;
}
#endif

static bool nc__renderer_queue_buffer_upload_internal(
    nc_renderer_t* renderer,
    nc_renderer_buffer_t* buffer,
    const void* data,
    const uint32_t size
) {
    NC_ASSERT(size);

    if (renderer->upload_count == renderer->upload_capacity) {
        const uint32_t new_capacity = nc__renderer_next_capacity(
                renderer->upload_capacity,
                renderer->upload_count + 1,
                16);
        renderer->upload_ops = realloc(renderer->upload_ops, new_capacity * sizeof(*renderer->upload_ops));
        renderer->upload_capacity = new_capacity;
    }

    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, 4, &offset)) {
        return false;
    }

    memcpy((uint8_t*)renderer->mapped_transfer_buffer + offset, data, size);
    renderer->upload_ops[renderer->upload_count++] = (nc__renderer_upload_op_t){
        .kind = NC__RENDERER_UPLOAD_BUFFER,
        .source_offset = offset,
        .size = size,
        .buffer = buffer,
    };
    buffer->queued_upload_frame = renderer->frame_id;
    renderer->uploads_dirty = true;
    return true;
}

static bool nc__renderer_queue_texture_upload(
    nc_renderer_t* renderer,
    nc_renderer_texture_t* texture,
    const uint16_t layer,
    const void* data,
    const uint32_t size
) {
    NC_ASSERT(size);

    if (renderer->upload_count == renderer->upload_capacity) {
        const uint32_t new_capacity = nc__renderer_next_capacity(
                renderer->upload_capacity,
                renderer->upload_count + 1,
                16);
        renderer->upload_ops = realloc(renderer->upload_ops, new_capacity * sizeof(*renderer->upload_ops));
        renderer->upload_capacity = new_capacity;
    }

    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, 16, &offset)) {
        return false;
    }

    memcpy((uint8_t*)renderer->mapped_transfer_buffer + offset, data, size);
    renderer->upload_ops[renderer->upload_count++] = (nc__renderer_upload_op_t){
        .kind = NC__RENDERER_UPLOAD_TEXTURE,
        .source_offset = offset,
        .size = size,
        .texture = {
            .texture = texture,
            .layer = layer,
        },
    };
    renderer->uploads_dirty = true;
    return true;
}

static nc_renderer_texture_t* nc__renderer_create_texture_object(
    const nc_renderer_t* renderer,
    const VkFormat format,
    const int16_t width,
    const int16_t height,
    const uint16_t layer_count,
    const bool is_array
) {
    NC_ASSERT(width > 0);
    NC_ASSERT(height > 0);

    nc_renderer_texture_t* result = calloc(1, sizeof(*result));
    result->format = format;
    result->width = width;
    result->height = height;
    result->layer_count = layer_count;
    result->is_array = is_array;
    result->layout = VK_IMAGE_LAYOUT_UNDEFINED;

    NC__CHECK_VK_RESULT(vmaCreateImage(
            renderer->allocator,
            &(VkImageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = format,
                .extent = { (uint32_t)width, (uint32_t)height, 1 },
                .mipLevels = 1,
                .arrayLayers = layer_count,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
            &(VmaAllocationCreateInfo){
                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
            },
            &result->image,
            &result->allocation,
            NULL));

    NC__CHECK_VK_RESULT(vkCreateImageView(
            renderer->device,
            &(VkImageViewCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = result->image,
                .viewType = is_array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                .format = format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = layer_count,
                },
            },
            NULL,
            &result->image_view));
    return result;

error:
    if (result) {
        vkDestroyImageView(renderer->device, result->image_view, NULL);
        if (result->image) {
            vmaDestroyImage(renderer->allocator, result->image, result->allocation);
        }
    }
    free(result);
    return NULL;
}

static void nc__renderer_destroy_texture_object(nc_renderer_t* renderer, nc_renderer_texture_t* texture) {
    if (!texture) {
        return;
    }

    if (renderer->frame_in_progress || renderer->frame_fence_pending) {
        nc__renderer_wait_idle(renderer);
    }

    vkDestroyImageView(renderer->device, texture->image_view, NULL);
    if (texture->image) {
        vmaDestroyImage(renderer->allocator, texture->image, texture->allocation);
    }
    free(texture);
}

static bool nc__renderer_draw_chunk_opaque(nc_renderer_t* renderer, const nc_renderer_chunk_opaque_draw_t* draw) {
    if (draw->vertex_count == 0) {
        return true;
    }

    NC_ASSERT(draw->texture->is_array);

    VkDescriptorSet quad_set;
    VkDescriptorSet uniform_set;
    VkDescriptorSet texture_set;
    const nc__renderer_chunk_uniforms_t uniforms = {
        .view_projection = *draw->view_projection,
        .position = draw->position,
    };
    if (!nc__renderer_write_storage_descriptor_set(renderer, draw->chunk_buffer, &quad_set) ||
            !nc__renderer_write_uniform_descriptor_set(renderer, 1, &uniforms, sizeof(uniforms), &uniform_set) ||
            !nc__renderer_write_texture_descriptor_set(
            renderer,
            draw->texture,
            draw->face_data_buffer,
            renderer->chunk_sampler,
            &texture_set)) {
        return false;
    }

    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            0,
            1,
            &quad_set,
            0,
            NULL);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            1,
            1,
            &uniform_set,
            0,
            NULL);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            2,
            1,
            &texture_set,
            0,
            NULL);
    vkCmdDraw(renderer->frame_command_buffer, draw->vertex_count, 1, 0, 0);
    return true;
}

static bool nc__renderer_draw_block_highlight(
    nc_renderer_t* renderer,
    const nc_renderer_block_highlight_draw_t* draw
) {
    if (!draw->shown) {
        return true;
    }

    const nc__renderer_block_highlight_vertex_uniforms_t vertex_uniforms = {
        .view_projection = *draw->view_projection,
        .block_position_and_scale = { { draw->position.x, draw->position.y, draw->position.z, 1.02f } },
    };

    const vkm_ubvec4 color = nc_cvar_get_block_highlight_color();
    const nc__renderer_block_highlight_fragment_uniforms_t fragment_uniforms = {
        .color = {
            .r = (float)color.r / 255.0f,
            .g = (float)color.g / 255.0f,
            .b = (float)color.b / 255.0f,
            .a = (float)color.a / 255.0f,
        },
        .time = draw->time,
    };

    VkPipeline pipeline;
    switch (nc_cvar_get_block_highlight_effect()) {
        case NC_BLOCK_HIGHLIGHT_EFFECT_OUTLINE:
            pipeline = renderer->outline_block_highlight_pipeline;
            break;
        case NC_BLOCK_HIGHLIGHT_EFFECT_VIGNETTE:
            pipeline = renderer->vignette_block_highlight_pipeline;
            break;
        case NC_BLOCK_HIGHLIGHT_EFFECT_PLASMA:
            pipeline = renderer->plasma_block_highlight_pipeline;
            break;
        default:
            NC_ASSERT(false);
            pipeline = renderer->outline_block_highlight_pipeline;
            break;
    }

    VkDescriptorSet vertex_uniform_set;
    VkDescriptorSet fragment_uniform_set;
    if (!nc__renderer_write_uniform_descriptor_set(
            renderer,
            1,
            &vertex_uniforms,
            sizeof(vertex_uniforms),
            &vertex_uniform_set) ||
            !nc__renderer_write_uniform_descriptor_set(
            renderer,
            3,
            &fragment_uniforms,
            sizeof(fragment_uniforms),
            &fragment_uniform_set)) {
        return false;
    }

    vkCmdBindPipeline(renderer->frame_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            1,
            1,
            &vertex_uniform_set,
            0,
            NULL);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            3,
            1,
            &fragment_uniform_set,
            0,
            NULL);
    vkCmdDraw(renderer->frame_command_buffer, 36, 1, 0, 0);
    return true;
}

static bool nc__renderer_draw_overlay(nc_renderer_t* renderer, const nc_renderer_overlay_draw_t* draw) {
    if (draw->draw_command_count == 0) {
        return true;
    }

    const float uniforms[] = {
        2.0f / (float)renderer->viewport.x,
        2.0f / (float)renderer->viewport.y,
        -1.0f,
        -1.0f,
    };
    VkDescriptorSet uniform_set;
    if (!nc__renderer_write_uniform_descriptor_set(renderer, 1, uniforms, sizeof(uniforms), &uniform_set)) {
        return false;
    }

    vkCmdBindPipeline(renderer->frame_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->gui_pipeline);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            1,
            1,
            &uniform_set,
            0,
            NULL);

    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(renderer->frame_command_buffer, 0, 1, &draw->vertex_buffer->buffer, &vertex_offset);
    vkCmdBindIndexBuffer(renderer->frame_command_buffer, draw->index_buffer->buffer, 0, VK_INDEX_TYPE_UINT16);

    for (uint32_t i = 0; i < draw->draw_command_count; i++) {
        const nc_renderer_overlay_draw_command_t* draw_command = &draw->draw_commands[i];
        const nc_renderer_texture_t* texture = draw_command->texture;
        if (!texture || texture->is_array) {
            continue;
        }

        VkDescriptorSet texture_set;
        if (!nc__renderer_write_texture_descriptor_set(renderer, texture, NULL, renderer->gui_sampler, &texture_set)) {
            return false;
        }
        vkCmdBindDescriptorSets(
                renderer->frame_command_buffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                renderer->pipeline_layout,
                2,
                1,
                &texture_set,
                0,
                NULL);
        nc__renderer_set_viewport_and_scissor(renderer, &draw_command->clip_rect);
        vkCmdDrawIndexed(
                renderer->frame_command_buffer,
                draw_command->element_count,
                1,
                draw_command->first_index,
                0,
                0);
    }

    return true;
}

static bool nc__renderer_draw_procedural_overlay(
    nc_renderer_t* renderer,
    const nc_renderer_procedural_overlay_draw_t* draw
) {
    if (!draw) {
        return true;
    }

    nc__renderer_procedural_overlay_uniforms_t uniforms = { 0 };
    for (uint8_t i = 0; i < 2; i++) {
        if (!draw->analog_sticks_active[i]) {
            continue;
        }

        uniforms.rings[i][0] = draw->analog_stick_ring_positions[i].x;
        uniforms.rings[i][1] = draw->analog_stick_ring_positions[i].y;
        uniforms.rings[i][2] = draw->analog_stick_ring_radius;
        uniforms.rings[i][3] = draw->analog_stick_ring_thickness;

        uniforms.sticks[i][0] = draw->analog_stick_positions[i].x;
        uniforms.sticks[i][1] = draw->analog_stick_positions[i].y;
        uniforms.sticks[i][2] = draw->analog_stick_radius;
    }

    uniforms.crosshair[0] = ((float)renderer->viewport.x - draw->crosshair_size) * 0.5f;
    uniforms.crosshair[1] = ((float)renderer->viewport.y - draw->crosshair_size) * 0.5f;
    uniforms.crosshair[2] = draw->crosshair_size;
    uniforms.crosshair[3] = draw->crosshair_size;

    VkDescriptorSet uniform_set;
    VkDescriptorSet texture_set;
    if (!nc__renderer_write_uniform_descriptor_set(renderer, 3, &uniforms, sizeof(uniforms), &uniform_set) ||
            !nc__renderer_write_texture_descriptor_set(
            renderer,
            renderer->procedural_overlay_crosshair_texture,
            NULL,
            renderer->gui_sampler,
            &texture_set)) {
        return false;
    }

    nc__renderer_set_viewport_and_scissor(renderer, NULL);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            3,
            1,
            &uniform_set,
            0,
            NULL);

    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->procedural_overlay_invert_pipeline);
    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            2,
            1,
            &texture_set,
            0,
            NULL);
    vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);

    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->procedural_overlay_stick_pipeline);
    vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);
    return true;
}

nc_renderer_t* nc_renderer_init(const nc_renderer_create_info_t* info) {
    nc_renderer_t* result = calloc(1, sizeof(*result));
    result->foreground = true;
    result->queue_family_index = UINT32_MAX;

    bool sdl_result = SDL_InitSubSystem(SDL_INIT_VIDEO);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_Vulkan_LoadLibrary(NULL);
    NC_CHECK_SDL_RESULT(sdl_result);
    SDL_FunctionPointer vk_get_instance_proc_addr = SDL_Vulkan_GetVkGetInstanceProcAddr();
    NC_CHECK_SDL_RESULT(vk_get_instance_proc_addr);
    volkInitializeCustom((PFN_vkGetInstanceProcAddr)vk_get_instance_proc_addr);

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    if (!nc__renderer_create_instance(result) ||
            !nc__renderer_create_window(result, info) ||
            !nc__renderer_create_surface(result) ||
            !nc__renderer_select_physical_device(result) ||
            !nc__renderer_create_device(result) ||
            !nc__renderer_create_descriptor_set_layouts(result) ||
            !nc__renderer_create_descriptor_pool(result) ||
            !nc__renderer_create_frame_resources(result) ||
            !nc__renderer_create_transfer_buffer(result, NC__RENDERER_INITIAL_TRANSFER_CAPACITY) ||
            !nc__renderer_create_sampler(result, &result->chunk_sampler) ||
            !nc__renderer_create_sampler(result, &result->gui_sampler) ||
            !nc__renderer_create_swapchain(result) ||
            !nc__renderer_create_pipelines(result)) {
        goto error;
    }

    result->dummy_storage_buffer = nc_renderer_create_buffer(
            result,
            NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ,
            sizeof(uint32_t));
    NC_CHECK_RESULT(result->dummy_storage_buffer, "Failed to create the dummy storage buffer.");

    result->procedural_overlay_crosshair_texture =
            nc_renderer_create_texture_2d_from_file(result, nc__renderer_crosshair_texture_path);
    NC_CHECK_RESULT(result->procedural_overlay_crosshair_texture, "Failed to load the procedural crosshair texture.");

    SDL_Log("Vulkan renderer initialized on %s.", result->physical_device_properties.deviceName);
    return result;

error:
    nc_renderer_fini(result);
    return NULL;
}

bool nc_renderer_handle_event(nc_renderer_t* renderer, const SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_MINIMIZED:
            renderer->foreground = false;
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            renderer->foreground = true;
            renderer->swapchain_dirty = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (event->window.data1 > 0 && event->window.data2 > 0) {
                renderer->window_size.x = (uint16_t)event->window.data1;
                renderer->window_size.y = (uint16_t)event->window.data2;
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event->window.data1 > 0 && event->window.data2 > 0) {
                renderer->viewport.x = (uint16_t)event->window.data1;
                renderer->viewport.y = (uint16_t)event->window.data2;
                renderer->swapchain_dirty = true;
            }
            break;
        default:
            break;
    }

    return true;
}

bool nc_renderer_begin_frame(nc_renderer_t* renderer) {
    renderer->frame_id++;
    NC__CHECK_VK_RESULT(vkWaitForFences(renderer->device, 1, &renderer->frame_fence, VK_TRUE, UINT64_MAX));
    renderer->frame_fence_pending = false;
    nc__renderer_destroy_retired_transfer_buffers(renderer);

    if (!renderer->uploads_dirty && renderer->upload_count == 0) {
        renderer->transfer_size = 0;
    }

    NC__CHECK_VK_RESULT(vkResetCommandPool(renderer->device, renderer->command_pool, 0));
    NC__CHECK_VK_RESULT(vkResetDescriptorPool(renderer->device, renderer->frame_descriptor_pool, 0));
    renderer->frame_descriptor_set_count = 0;

    if ((renderer->swapchain_dirty || renderer->surface_dirty) && !nc__renderer_recreate_swapchain(renderer)) {
        goto error;
    }

    vkResetCommandBuffer(renderer->frame_command_buffer, 0);
    NC__CHECK_VK_RESULT(vkBeginCommandBuffer(
            renderer->frame_command_buffer,
            &(VkCommandBufferBeginInfo){
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            }));
    renderer->frame_in_progress = true;

    renderer->frame_has_swapchain_image = false;
    renderer->frame_swapchain_image_index = 0;
    if (!renderer->foreground ||
            renderer->swapchain == VK_NULL_HANDLE ||
            renderer->viewport.x == 0 ||
            renderer->viewport.y == 0) {
        return true;
    }

    const VkResult acquire_result = vkAcquireNextImageKHR(
            renderer->device,
            renderer->swapchain,
            UINT64_MAX,
            renderer->image_available_semaphore,
            VK_NULL_HANDLE,
            &renderer->frame_swapchain_image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        renderer->swapchain_dirty = true;
        renderer->frame_has_swapchain_image = false;
        return true;
    }
    if (acquire_result == VK_ERROR_SURFACE_LOST_KHR) {
        renderer->surface_dirty = true;
        renderer->swapchain_dirty = true;
        renderer->frame_has_swapchain_image = false;
        return true;
    }
    NC_CHECK_RESULT(
            acquire_result == VK_SUCCESS || acquire_result == VK_SUBOPTIMAL_KHR,
            "vkAcquireNextImageKHR failed with %s.",
            nc__renderer_vk_result_string(acquire_result));

#ifndef ANDROID
    if (acquire_result == VK_SUBOPTIMAL_KHR) {
        renderer->swapchain_dirty = true;
    }
#endif

    renderer->frame_has_swapchain_image = true;
    return true;

error:
    return false;
}

bool nc_renderer_end_frame(nc_renderer_t* renderer) {
    NC_ASSERT(renderer->frame_command_buffer);

    if (renderer->transfer_size > 0) {
        NC__CHECK_VK_RESULT(vmaFlushAllocation(
                renderer->allocator,
                renderer->transfer_allocation,
                0,
                renderer->transfer_size));
    }

    NC__CHECK_VK_RESULT(vkEndCommandBuffer(renderer->frame_command_buffer));
    NC__CHECK_VK_RESULT(vkResetFences(renderer->device, 1, &renderer->frame_fence));

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore* render_finished_semaphore = renderer->frame_has_swapchain_image
            ? &renderer->render_finished_semaphores[renderer->frame_swapchain_image_index]
            : NULL;
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = renderer->frame_has_swapchain_image ? 1u : 0u,
        .pWaitSemaphores = renderer->frame_has_swapchain_image ? &renderer->image_available_semaphore : NULL,
        .pWaitDstStageMask = renderer->frame_has_swapchain_image ? &wait_stage : NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &renderer->frame_command_buffer,
        .signalSemaphoreCount = renderer->frame_has_swapchain_image ? 1u : 0u,
        .pSignalSemaphores = render_finished_semaphore,
    };
    NC__CHECK_VK_RESULT(vkQueueSubmit(renderer->queue, 1, &submit_info, renderer->frame_fence));
    renderer->frame_fence_pending = true;

    if (renderer->frame_has_swapchain_image) {
        const VkResult present_result = vkQueuePresentKHR(
                renderer->queue,
                &(VkPresentInfoKHR){
                    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = render_finished_semaphore,
                    .swapchainCount = 1,
                    .pSwapchains = &renderer->swapchain,
                    .pImageIndices = &renderer->frame_swapchain_image_index,
                });
        if (present_result == VK_ERROR_SURFACE_LOST_KHR) {
            renderer->surface_dirty = true;
            renderer->swapchain_dirty = true;
        } else if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            renderer->swapchain_dirty = true;
        } else {
            NC__CHECK_VK_RESULT(present_result);
        }
    }

    renderer->frame_in_progress = false;
    renderer->frame_has_swapchain_image = false;
    return true;

error:
    renderer->frame_in_progress = false;
    return false;
}

bool nc_renderer_set_relative_mouse_mode(nc_renderer_t* renderer, const bool enabled) {
    const bool sdl_result = SDL_SetWindowRelativeMouseMode(renderer->window, enabled);
    NC_CHECK_SDL_RESULT(sdl_result);
    return true;

error:
    return false;
}

bool nc_renderer_is_foreground(const nc_renderer_t* renderer) {
    return renderer->foreground;
}

vkm_usvec2 nc_renderer_get_viewport(const nc_renderer_t* renderer) {
    return renderer->viewport;
}

vkm_usvec2 nc_renderer_get_window_size(const nc_renderer_t* renderer) {
    return renderer->window_size;
}

nc_renderer_buffer_t* nc_renderer_create_buffer(
    nc_renderer_t* renderer,
    const nc_renderer_buffer_usage_t usage,
    const uint32_t initial_size
) {
    static const VkBufferUsageFlags nc_to_vk_usage[] = {
        [NC_RENDERER_BUFFER_USAGE_VERTEX] = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        [NC_RENDERER_BUFFER_USAGE_INDEX] = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        [NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ] = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };

    NC_ASSERT(usage > 0 && usage <= NC_RENDERER_BUFFER_USAGE_COUNT);
    NC_ASSERT(initial_size > 0);

    nc_renderer_buffer_t* result = calloc(1, sizeof(*result));
    result->usage = nc_to_vk_usage[usage] | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    result->capacity = initial_size;

    NC__CHECK_VK_RESULT(vmaCreateBuffer(
            renderer->allocator,
            &(VkBufferCreateInfo){
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = initial_size,
                .usage = result->usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            },
            &(VmaAllocationCreateInfo){
                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
            },
            &result->buffer,
            &result->allocation,
            NULL));
    return result;

error:
    free(result);
    return NULL;
}

void nc_renderer_destroy_buffer(nc_renderer_t* renderer, nc_renderer_buffer_t* buffer) {
    if (!buffer) {
        return;
    }

    if (renderer->frame_in_progress || renderer->frame_fence_pending) {
        nc__renderer_wait_idle(renderer);
    }

    vmaDestroyBuffer(renderer->allocator, buffer->buffer, buffer->allocation);
    free(buffer);
}

bool nc_renderer_queue_buffer_upload(
    nc_renderer_t* renderer,
    nc_renderer_buffer_t* buffer,
    const void* data,
    const uint32_t size
) {
    if (size > buffer->capacity) {
        const uint32_t new_capacity = nc__renderer_next_capacity(buffer->capacity, size, buffer->capacity);
        VkBuffer new_buffer;
        VmaAllocation new_allocation;
        NC__CHECK_VK_RESULT(vmaCreateBuffer(
                renderer->allocator,
                &(VkBufferCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = new_capacity,
                    .usage = buffer->usage,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                },
                &(VmaAllocationCreateInfo){
                    .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                },
                &new_buffer,
                &new_allocation,
                NULL));

        vmaDestroyBuffer(renderer->allocator, buffer->buffer, buffer->allocation);
        buffer->buffer = new_buffer;
        buffer->allocation = new_allocation;
        buffer->capacity = new_capacity;
    }

    return nc__renderer_queue_buffer_upload_internal(renderer, buffer, data, size);

error:
    return false;
}

nc_renderer_texture_t* nc_renderer_create_rgba_texture_2d(
    nc_renderer_t* renderer,
    const int16_t width,
    const int16_t height,
    const void* pixels
) {
    nc_renderer_texture_t* result = nc__renderer_create_texture_object(
            renderer,
            VK_FORMAT_R8G8B8A8_UNORM,
            width,
            height,
            1,
            false);
    if (!result) {
        return NULL;
    }

    const uint32_t size = (uint32_t)width * (uint32_t)height * 4;
    if (!nc__renderer_queue_texture_upload(renderer, result, 0, pixels, size)) {
        nc__renderer_destroy_texture_object(renderer, result);
        return NULL;
    }

    return result;
}

nc_renderer_texture_t* nc_renderer_create_texture_2d_from_file(nc_renderer_t* renderer, const char* path) {
    nc__renderer_texture_file_data_t texture_data;
    if (!nc__renderer_load_texture_file(path, &texture_data)) {
        return NULL;
    }

    nc_renderer_texture_t* texture = nc__renderer_create_texture_object(
            renderer,
            texture_data.format,
            texture_data.width,
            texture_data.height,
            1,
            false);
    if (!texture) {
        nc__renderer_free_texture_file_data(&texture_data);
        return NULL;
    }

    const bool upload_result = nc__renderer_queue_texture_upload(
            renderer,
            texture,
            0,
            texture_data.bytes,
            texture_data.size);
    nc__renderer_free_texture_file_data(&texture_data);
    if (!upload_result) {
        nc__renderer_destroy_texture_object(renderer, texture);
        return NULL;
    }

    return texture;
}

nc_renderer_texture_t* nc_renderer_create_texture_array_from_files(
    nc_renderer_t* renderer,
    const char* const* paths,
    const uint16_t path_count
) {
    nc__renderer_texture_file_data_t texture_data;
    if (!nc__renderer_load_texture_file(paths[0], &texture_data)) {
        return NULL;
    }

    nc_renderer_texture_t* result = nc__renderer_create_texture_object(
            renderer,
            texture_data.format,
            texture_data.width,
            texture_data.height,
            path_count,
            true);
    if (!result) {
        nc__renderer_free_texture_file_data(&texture_data);
        return NULL;
    }

    bool upload_result = nc__renderer_queue_texture_upload(renderer, result, 0, texture_data.bytes, texture_data.size);
    nc__renderer_free_texture_file_data(&texture_data);
    if (!upload_result) {
        nc__renderer_destroy_texture_object(renderer, result);
        return NULL;
    }

    for (uint16_t i = 1; i < path_count; i++) {
        if (!nc__renderer_load_texture_file(paths[i], &texture_data)) {
            nc__renderer_destroy_texture_object(renderer, result);
            return NULL;
        }

        const bool compatible =
                texture_data.format == result->format &&
                texture_data.width == result->width &&
                texture_data.height == result->height;
        if (!compatible) {
            NC_SET_ERROR("Texture array layer %u does not match the first layer.", i);
            nc__renderer_free_texture_file_data(&texture_data);
            nc__renderer_destroy_texture_object(renderer, result);
            return NULL;
        }

        upload_result = nc__renderer_queue_texture_upload(renderer, result, i, texture_data.bytes, texture_data.size);
        nc__renderer_free_texture_file_data(&texture_data);
        if (!upload_result) {
            nc__renderer_destroy_texture_object(renderer, result);
            return NULL;
        }
    }

    return result;
}

void nc_renderer_destroy_texture(nc_renderer_t* renderer, nc_renderer_texture_t* texture) {
    nc__renderer_destroy_texture_object(renderer, texture);
}

bool nc_renderer_draw(nc_renderer_t* renderer, const nc_renderer_frame_t* frame) {
    NC_ASSERT(renderer->frame_command_buffer);

    if (!nc__renderer_flush_uploads(renderer)) {
        return false;
    }

    if (!renderer->frame_has_swapchain_image) {
        return true;
    }

    const VkClearValue clear_values[] = {
        {
            .color = {
                .float32 = {
                    NC__RENDERER_CLEAR_RED,
                    NC__RENDERER_CLEAR_GREEN,
                    NC__RENDERER_CLEAR_BLUE,
                    1.0f,
                },
            },
        },
        {
            .depthStencil = { .depth = 1.0f },
        },
    };
    vkCmdBeginRenderPass(
            renderer->frame_command_buffer,
            &(VkRenderPassBeginInfo){
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = renderer->render_pass,
                .framebuffer = renderer->framebuffers[renderer->frame_swapchain_image_index],
                .renderArea = {
                    .extent = renderer->swapchain_extent,
                },
                .clearValueCount = NC_COUNTOF(clear_values),
                .pClearValues = clear_values,
            },
            VK_SUBPASS_CONTENTS_INLINE);

    nc__renderer_set_viewport_and_scissor(renderer, NULL);
    vkCmdBindPipeline(renderer->frame_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->chunk_pipeline);
    for (uint32_t i = 0; i < nc_renderer_chunk_opaque_draw_vec_count(frame->opaque_draws); i++) {
        const nc_renderer_chunk_opaque_draw_t draw = nc_renderer_chunk_opaque_draw_vec_get(frame->opaque_draws, i);
        if (!nc__renderer_draw_chunk_opaque(renderer, &draw)) {
            return false;
        }
    }
    if (!nc__renderer_draw_block_highlight(renderer, frame->block_highlight_draw)) {
        return false;
    }
    for (uint32_t i = 0; i < frame->overlay_draw_count; i++) {
        if (!nc__renderer_draw_overlay(renderer, &frame->overlay_draws[i])) {
            return false;
        }
    }
    if (!nc__renderer_draw_procedural_overlay(renderer, frame->procedural_overlay_draw)) {
        return false;
    }

    vkCmdEndRenderPass(renderer->frame_command_buffer);
    return true;
}

float nc_renderer_get_window_pixel_density(const nc_renderer_t* renderer) {
    const float result = SDL_GetWindowPixelDensity(renderer->window);
    if (result == 0.0f) {
        SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "SDL_GetWindowPixelDensity() failed, falling back to 1.0f: %s",
                SDL_GetError());
        SDL_ClearError();
        return 1.0f;
    }

    return result;
}

float nc_renderer_get_window_display_scale(const nc_renderer_t* renderer) {
    const float result = SDL_GetWindowDisplayScale(renderer->window);
    if (result == 0.0f) {
        SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "SDL_GetWindowDisplayScale() failed, falling back to pixel density: %s",
                SDL_GetError());
        SDL_ClearError();
        return nc_renderer_get_window_pixel_density(renderer);
    }

    return result;
}

void nc_renderer_get_window_safe_area(const nc_renderer_t* renderer, SDL_Rect* rect) {
    int count;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (count == 0 || !windows[0] || !SDL_GetWindowSafeArea(windows[0], rect)) {
        SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Failed to get the window's safe area. Falling back to the entire window.");
        const vkm_usvec2 window_size = nc_renderer_get_window_size(renderer);
        *rect = (SDL_Rect){ .x = 0, .y = 0, .w = window_size.x, .h = window_size.y };
    }
    SDL_free((void*)windows);
}

void nc_renderer_fini(nc_renderer_t* renderer) {
    if (!renderer) {
        return;
    }

    if (renderer->device) {
        nc__renderer_wait_idle(renderer);

        nc__renderer_destroy_descriptor_state(renderer);
        nc__renderer_destroy_texture_object(renderer, renderer->procedural_overlay_crosshair_texture);
        nc_renderer_destroy_buffer(renderer, renderer->dummy_storage_buffer);

        nc__renderer_destroy_pipelines(renderer);
        vkDestroySampler(renderer->device, renderer->chunk_sampler, NULL);
        vkDestroySampler(renderer->device, renderer->gui_sampler, NULL);
        nc__renderer_destroy_swapchain(renderer);
        vkDestroyRenderPass(renderer->device, renderer->render_pass, NULL);
        nc__renderer_destroy_retired_transfer_buffers(renderer);
        if (renderer->transfer_buffer) {
            vmaDestroyBuffer(renderer->allocator, renderer->transfer_buffer, renderer->transfer_allocation);
        }
        vkDestroySemaphore(renderer->device, renderer->image_available_semaphore, NULL);
        vkDestroyFence(renderer->device, renderer->frame_fence, NULL);
        vkDestroyCommandPool(renderer->device, renderer->command_pool, NULL);
        vmaDestroyAllocator(renderer->allocator);
        vkDestroyDevice(renderer->device, NULL);
    }

    if (renderer->surface) {
        SDL_Vulkan_DestroySurface(renderer->instance, renderer->surface, NULL);
    }
    if (renderer->instance) {
        vkDestroyInstance(renderer->instance, NULL);
    }

    free(renderer->retired_transfer_buffers);
    free(renderer->upload_ops);
    SDL_DestroyWindow(renderer->window);
    SDL_Vulkan_UnloadLibrary();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    free(renderer);
}
