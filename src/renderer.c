#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
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

#include <novacube/asset_manager.h>
#include <novacube/build_info.h>
#include <novacube/error_handling.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>

#ifdef ANDROID
#define NC__RENDERER_PREFERRED_SWAPCHAIN_FORMAT VK_FORMAT_R8G8B8A8_SRGB
#define NC__RENDERER_ALTERNATIVE_SWAPCHAIN_FORMAT VK_FORMAT_B8G8R8A8_SRGB
#else
#define NC__RENDERER_PREFERRED_SWAPCHAIN_FORMAT VK_FORMAT_B8G8R8A8_SRGB
#define NC__RENDERER_ALTERNATIVE_SWAPCHAIN_FORMAT VK_FORMAT_R8G8B8A8_SRGB
#endif

#define NC__RENDERER_VK_API_VERSION VK_API_VERSION_1_1
enum {
    NC__RENDERER_TRANSFER_PAGE_CAPACITY = 1024 * 1024,
    NC__RENDERER_TRANSFER_PAGE_RETENTION_FRAMES = 60,
    NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS = 256,
    NC__RENDERER_BUFFER_REFERENCE_ALIGNMENT = 16,
    NC__RENDERER_VERTEX_PUSH_CONSTANT_OFFSET = 0,
    NC__RENDERER_FRAGMENT_ELEMENT_PUSH_CONSTANT_OFFSET = 8,
    NC__RENDERER_FRAGMENT_PUSH_CONSTANT_OFFSET = 16,
};
// TODO: Maybe this value could be tuned down for devices with a `subPixelPrecisionBits` property higher than 4.
// Need testing on more devices.
#define NC__RENDERER_QUAD_EXPANSION_PIXELS 0.1f
#define NC__RENDERER_DEPTH_FORMAT VK_FORMAT_D16_UNORM
// Asset texture dimensions are limited to INT16_MAX, which can produce at most 15 mip levels.
#define NC__RENDERER_MAX_TEXTURE_MIP_LEVELS 15

#define NC__CHECK_VK_RESULT(result) do { \
    const VkResult nc__vk_result = (result); \
    if (nc__vk_result != VK_SUCCESS) { \
        SDL_ClearError(); \
        NC_SET_ERROR(nc__renderer_vk_result_string(nc__vk_result)); \
        goto error; \
    } \
} while (false)

static float nc__renderer_srgb_to_linear(const float srgb) {
    if (srgb <= 0.04045f) {
        return srgb * (1.0f / 12.92f);
    }

    return powf((srgb + 0.055f) * (1.0f / 1.055f), 2.4f);
}

static uint8_t nc__renderer_mip_level_count(uint32_t width, uint32_t height) {
    uint32_t largest_dimension = width > height ? width : height;
    uint8_t result = 1;
    while (largest_dimension > 1) {
        largest_dimension >>= 1;
        result++;
    }
    NC_ASSERT(result <= NC__RENDERER_MAX_TEXTURE_MIP_LEVELS);
    return result;
}

static size_t nc__renderer_block_compressed_mip_chain_size(
    uint32_t width,
    uint32_t height,
    const uint8_t mip_level_count
) {
    size_t result = 0;
    for (uint8_t mip_level = 0; mip_level < mip_level_count; mip_level++) {
        result += ((size_t)width + 3) / 4 * (((size_t)height + 3) / 4) * 16;
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return result;
}

typedef uint8_t nc__renderer_upload_kind_t;
enum {
    NC__RENDERER_UPLOAD_BUFFER = 1,
    NC__RENDERER_UPLOAD_TEXTURE,
};

typedef struct nc__renderer_upload_op_t {
    union {
        nc_renderer_buffer_t* buffer;
        struct {
            nc_renderer_texture_t* texture;
            uint16_t layer;
        } texture;
    };
    uint32_t source_page;
    uint32_t source_offset;
    uint32_t size;
    nc__renderer_upload_kind_t kind;
} nc__renderer_upload_op_t;

typedef struct nc__renderer_transfer_page_t {
    VkBuffer buffer;
    VkDeviceAddress address;
    VmaAllocation allocation;
    void* mapping;
    uint64_t last_used_frame;
    uint32_t size;
    uint32_t capacity;
} nc__renderer_transfer_page_t;

typedef struct nc__renderer_retired_buffer_t {
    VkBuffer buffer;
    VmaAllocation allocation;
} nc__renderer_retired_buffer_t;

typedef struct nc__renderer_retired_texture_t {
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
} nc__renderer_retired_texture_t;

typedef struct nc__renderer_retired_swapchain_t {
    VkSwapchainKHR swapchain;
    VkSemaphore* present_semaphores;
    uint32_t present_semaphore_count;
} nc__renderer_retired_swapchain_t;

typedef struct nc_renderer_texture_t {
    VkImage image;
    VmaAllocation allocation;
    VkImageView image_view;
    VkImageLayout layout;
    int16_t width;
    int16_t height;
    uint16_t layer_count;
    uint8_t mip_level_count;
} nc_renderer_texture_t;

typedef struct nc_renderer_buffer_t {
    VkBuffer buffer;
    VmaAllocation allocation;
    VkBufferUsageFlags usage;
    uint32_t capacity;
    VkDeviceAddress address;
} nc_renderer_buffer_t;

typedef struct nc__renderer_procedural_overlay_uniforms_t {
    float rings[2][4];
    float sticks[2][4];
    float crosshair[4];
} nc__renderer_procedural_overlay_uniforms_t;

typedef struct nc__renderer_gui_uniforms_t {
    vkm_mat4 transform;
    vkm_vec2 gui_to_ndc_scale;
} nc__renderer_gui_uniforms_t;

// The transform is constant for the frame; the rectangle address advances between ordered batches.
typedef struct nc__renderer_gui_push_constants_t {
    VkDeviceAddress uniforms;
    VkDeviceAddress rectangles;
} nc__renderer_gui_push_constants_t;

static_assert(sizeof(nc_renderer_overlay_rectangle_t) == 84, "GUI rectangle must match its scalar GLSL layout");
static_assert(
        offsetof(nc_renderer_overlay_rectangle_t, character) == 80,
        "GUI rectangle character offset must match GLSL");

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
    float sunlight_intensity;
    vkm_vec4 quad_expansion;
} nc__renderer_chunk_uniforms_t;

typedef struct nc__renderer_sky_uniforms_t {
    vkm_mat4 inverse_view_projection;
    vkm_vec4 camera_position;
    vkm_vec4 gradient_colors[NC_RENDERER_SKY_GRADIENT_COLOR_COUNT];
    vkm_vec4 gradient_stops;
} nc__renderer_sky_uniforms_t;

typedef struct nc__renderer_chunk_push_constants_t {
    VkDeviceAddress quad_buffer;
    VkDeviceAddress uniforms;
    VkDeviceAddress face_data_buffer;
} nc__renderer_chunk_push_constants_t;

typedef struct nc__renderer_address_push_constants_t {
    VkDeviceAddress data;
} nc__renderer_address_push_constants_t;

#define TDS_TYPE nc__renderer_transfer_page_vec
#define TDS_VALUE_T nc__renderer_transfer_page_t
#include <tds/vector.h>

#define TDS_TYPE nc__renderer_upload_op_vec
#define TDS_VALUE_T nc__renderer_upload_op_t
#include <tds/vector.h>

#define TDS_TYPE nc__renderer_retired_buffer_vec
#define TDS_VALUE_T nc__renderer_retired_buffer_t
#include <tds/vector.h>

#define TDS_TYPE nc__renderer_retired_texture_vec
#define TDS_VALUE_T nc__renderer_retired_texture_t
#include <tds/vector.h>

#define TDS_TYPE nc__renderer_retired_swapchain_vec
#define TDS_VALUE_T nc__renderer_retired_swapchain_t
#include <tds/vector.h>

typedef struct nc__renderer_image_t {
    VkImage image;
    VmaAllocation allocation;
    VkImageView view;
} nc__renderer_image_t;

typedef struct nc_renderer_t {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;
    PFN_vkGetBufferDeviceAddressKHR get_buffer_device_address;
    bool khr_get_buffer_device_address;
    VmaAllocator allocator;

    SDL_Window* window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    bool* swapchain_image_initialized;
    VkFramebuffer* framebuffers;
    uint32_t swapchain_image_count;
    vkm_usvec2 window_size;
    // Native-orientation dimensions of the images owned by the current Vulkan swapchain.
    // This is authoritative for rendering; SDL window dimensions belong to input coordinates.
    vkm_usvec2 swapchain_extent;
    VkSurfaceTransformFlagBitsKHR surface_transform;
    bool foreground;
    bool swapchain_dirty;
    bool surface_dirty;

    nc__renderer_image_t depth;
    nc__renderer_image_t accumulation;
    nc__renderer_image_t reveal;
    bool render_pass_attachments_initialized;

    VkRenderPass render_pass;
    VkDescriptorSetLayout texture_descriptor_set_layout;
    VkDescriptorSetLayout composite_descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipelineLayout composite_pipeline_layout;
    VkDescriptorPool frame_descriptor_pool;
    uint32_t frame_descriptor_set_count;

    VkPipeline opaque_chunk_pipeline;
    VkPipeline transparent_chunk_pipeline;
    VkPipeline composite_chunk_pipeline;
    VkPipeline gui_pipeline;
    VkPipeline gui_image_pipeline;
    VkPipeline procedural_overlay_invert_pipeline;
    VkPipeline procedural_overlay_stick_pipeline;
    VkPipeline outline_block_highlight_pipeline;
    VkPipeline vignette_block_highlight_pipeline;
    VkPipeline plasma_block_highlight_pipeline;
    VkPipeline sky_pipeline;
    VkSampler chunk_sampler;
    VkSampler gui_sampler;
    nc_renderer_texture_t* procedural_overlay_crosshair_texture;

    VkCommandPool command_pool;
    VkCommandBuffer frame_command_buffer;
    VkFence frame_fence;
    VkSemaphore image_available_semaphore;
    VkSemaphore* render_finished_semaphores;
    bool frame_in_progress;
    bool frame_fence_pending;
    bool frame_has_swapchain_image;
    // Extension-free present retirement follows the acquire-based proof described by Khronos:
    // https://docs.vulkan.org/samples/latest/samples/api/swapchain_recreation/README.html
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    // This frame reacquired the replacement image chosen below; its submit waits on that acquisition.
    bool frame_completes_present_retirement;
    // The proof submit is pending; its frame fence must complete before retired swapchains are destroyed.
    bool retired_swapchains_pending_destruction;
    // A replacement image has been presented and selected as the retirement proof.
    bool swapchain_retirement_probe_presented;
    uint32_t frame_swapchain_image_index;
    // Image selected by the first successful presentation on the replacement swapchain.
    uint32_t swapchain_retirement_probe_image_index;

    nc__renderer_transfer_page_vec transfer_pages;

    nc__renderer_upload_op_vec upload_ops;
    nc__renderer_retired_buffer_vec retired_buffers;
    nc__renderer_retired_texture_vec retired_textures;
    // Every old generation waiting on the current replacement's acquire-based retirement proof.
    nc__renderer_retired_swapchain_vec retired_swapchains;
    bool uploads_dirty;

    uint64_t frame_id;
} nc_renderer_t;

#define TDS_IMPLEMENT
#define TDS_VALUE_T nc_renderer_chunk_draw_t
#define TDS_TYPE nc_renderer_chunk_draw_vec
#include <tds/vector.h>

typedef struct nc__renderer_required_extension {
    const char* const* alternative_names;
} nc__renderer_required_extension;

static const nc__renderer_required_extension nc__renderer_required_extensions[] = {
    {
        .alternative_names = (const char* const []){ VK_KHR_SWAPCHAIN_EXTENSION_NAME, NULL },
    },
    {
        .alternative_names = (const char* const []){
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            NULL,
        },
    },
    {
        .alternative_names = (const char* const []){ VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME, NULL },
    },
};

#define NC__RENDERER_REQUIRED_EXTENSION_COUNT NC_COUNTOF(nc__renderer_required_extensions)

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
    NC_ASSERT(minimum);
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

static bool nc__renderer_surface_transform_swaps_extent(const VkSurfaceTransformFlagBitsKHR transform) {
    return transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
            transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
}

static void nc__renderer_get_pre_rotation_matrix(const nc_renderer_t* renderer, vkm_mat4* result) {
    const vkm_vec3 axis = CVKM_VEC3_FORWARD;
    float angle;

    switch (renderer->surface_transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
            angle = CVKM_PI_2_F;
            break;
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
            angle = CVKM_PI_F;
            break;
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
            angle = -CVKM_PI_2_F;
            break;
        default:
            *result = CVKM_MAT4_IDENTITY;
            return;
    }

    vkm_make_rotation(angle, &axis, result);
}

static void nc__renderer_pre_rotate_view_projection(
    const nc_renderer_t* renderer,
    const vkm_mat4* view_projection,
    vkm_mat4* result
) {
    vkm_mat4 pre_rotation;
    nc__renderer_get_pre_rotation_matrix(renderer, &pre_rotation);
    vkm_mul(&pre_rotation, view_projection, result);
}

static void nc__renderer_pre_rotate_rect(const nc_renderer_t* renderer, const SDL_Rect* rect, SDL_Rect* result) {
    const int x = rect->x;
    const int y = rect->y;
    const int w = rect->w;
    const int h = rect->h;
    const int buffer_width = renderer->swapchain_extent.x;
    const int buffer_height = renderer->swapchain_extent.y;

    switch (renderer->surface_transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
            *result = (SDL_Rect){ .x = buffer_width - h - y, .y = x, .w = h, .h = w };
            break;
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
            *result = (SDL_Rect){ .x = buffer_width - w - x, .y = buffer_height - h - y, .w = w, .h = h };
            break;
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
            *result = (SDL_Rect){ .x = y, .y = buffer_height - w - x, .w = h, .h = w };
            break;
        default:
            *result = *rect;
            break;
    }
}

static vkm_vec2 nc__renderer_pre_rotate_point(const nc_renderer_t* renderer, const vkm_vec2 point) {
    const float buffer_width = (float)renderer->swapchain_extent.x;
    const float buffer_height = (float)renderer->swapchain_extent.y;

    switch (renderer->surface_transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
            return (vkm_vec2){ { buffer_width - point.y, point.x } };
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
            return (vkm_vec2){ { buffer_width - point.x, buffer_height - point.y } };
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
            return (vkm_vec2){ { point.y, buffer_height - point.x } };
        default:
            return point;
    }
}

static VkDeviceAddress nc__renderer_get_buffer_address(const nc_renderer_t* renderer, VkBuffer buffer) {
    return renderer->get_buffer_device_address(
            renderer->device,
            &(VkBufferDeviceAddressInfo){
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .buffer = buffer,
            });
}

static void nc__renderer_wait_idle(nc_renderer_t* renderer) {
    if (!renderer->device) {
        return;
    }

    vkDeviceWaitIdle(renderer->device);
    renderer->frame_in_progress = false;
    renderer->frame_fence_pending = false;
}

static bool nc__renderer_create_transfer_page(
    nc_renderer_t* renderer,
    const uint32_t capacity,
    nc__renderer_transfer_page_t* page
) {
    VmaAllocationInfo allocation_info;
    VkBuffer buffer;
    VmaAllocation allocation;
    NC__CHECK_VK_RESULT(vmaCreateBuffer(
            renderer->allocator,
            &(VkBufferCreateInfo){
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = capacity,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            },
            &(VmaAllocationCreateInfo){
                .usage = VMA_MEMORY_USAGE_AUTO,
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            },
            &buffer,
            &allocation,
            &allocation_info));
    NC_ASSERT(allocation_info.pMappedData);
    *page = (nc__renderer_transfer_page_t){
        .buffer = buffer,
        .address = nc__renderer_get_buffer_address(renderer, buffer),
        .allocation = allocation,
        .mapping = allocation_info.pMappedData,
        .last_used_frame = renderer->frame_id,
        .capacity = capacity,
    };
    return true;

error:
    return false;
}

static bool nc__renderer_initialize_transfer_pages(nc_renderer_t* renderer) {
    nc__renderer_transfer_page_t page;
    if (!nc__renderer_create_transfer_page(renderer, NC__RENDERER_TRANSFER_PAGE_CAPACITY, &page)) {
        return false;
    }
    nc__renderer_transfer_page_vec_append(&renderer->transfer_pages, page);
    return true;
}

static void nc__renderer_reset_and_trim_transfer_pages(nc_renderer_t* renderer) {
    NC_ASSERT(!renderer->uploads_dirty && nc__renderer_upload_op_vec_count(&renderer->upload_ops) == 0);

    uint32_t regular_page_count = 0;
    const uint32_t page_count = nc__renderer_transfer_page_vec_count(&renderer->transfer_pages);
    for (uint32_t i = 0; i < page_count; i++) {
        nc__renderer_transfer_page_t* page = &renderer->transfer_pages.array[i];
        page->size = 0;
        regular_page_count += page->capacity == NC__RENDERER_TRANSFER_PAGE_CAPACITY;
    }
    uint32_t i = 0;
    while (i < nc__renderer_transfer_page_vec_count(&renderer->transfer_pages)) {
        const nc__renderer_transfer_page_t page = renderer->transfer_pages.array[i];
        const bool regular = page.capacity == NC__RENDERER_TRANSFER_PAGE_CAPACITY;
        const bool expired = renderer->frame_id - page.last_used_frame
                >= NC__RENDERER_TRANSFER_PAGE_RETENTION_FRAMES;
        if (!expired || (regular && regular_page_count <= 1)) {
            i++;
            continue;
        }

        vmaDestroyBuffer(renderer->allocator, page.buffer, page.allocation);
        if (regular) {
            regular_page_count--;
        }
        const uint32_t last = nc__renderer_transfer_page_vec_count(&renderer->transfer_pages) - 1;
        if (i != last) {
            renderer->transfer_pages.array[i] = renderer->transfer_pages.array[last];
        }
        renderer->transfer_pages.count--;
    }
    NC_ASSERT(regular_page_count >= 1);
}

static bool nc__renderer_reserve_transfer_bytes(
    nc_renderer_t* renderer,
    const uint32_t size,
    const uint32_t alignment,
    uint32_t* out_page,
    uint32_t* out_offset
) {
    NC_ASSERT(size);
    NC_ASSERT(alignment);

    uint32_t best_page = UINT32_MAX;
    uint32_t best_offset = 0;
    uint32_t best_remaining = UINT32_MAX;
    const uint32_t page_count = nc__renderer_transfer_page_vec_count(&renderer->transfer_pages);
    for (uint32_t i = 0; i < page_count; i++) {
        const nc__renderer_transfer_page_t* page = &renderer->transfer_pages.array[i];
        const uint32_t remainder = page->size % alignment;
        const uint32_t padding = remainder ? alignment - remainder : 0;
        if (padding > page->capacity - page->size) {
            continue;
        }
        const uint32_t offset = page->size + padding;
        if (size > page->capacity - offset) {
            continue;
        }
        const uint32_t remaining = page->capacity - offset - size;
        if (remaining < best_remaining) {
            best_page = i;
            best_offset = offset;
            best_remaining = remaining;
        }
    }

    if (best_page == UINT32_MAX) {
        uint32_t capacity = NC__RENDERER_TRANSFER_PAGE_CAPACITY;
        if (size > capacity) {
            const uint32_t remainder = size % NC__RENDERER_TRANSFER_PAGE_CAPACITY;
            capacity = remainder && size <= UINT32_MAX - (NC__RENDERER_TRANSFER_PAGE_CAPACITY - remainder)
                    ? size + NC__RENDERER_TRANSFER_PAGE_CAPACITY - remainder
                    : size;
        }
        nc__renderer_transfer_page_t page;
        if (!nc__renderer_create_transfer_page(renderer, capacity, &page)) {
            return false;
        }
        best_page = nc__renderer_transfer_page_vec_count(&renderer->transfer_pages);
        nc__renderer_transfer_page_vec_append(&renderer->transfer_pages, page);
        best_offset = 0;
    }

    nc__renderer_transfer_page_t* page = &renderer->transfer_pages.array[best_page];
    page->size = best_offset + size;
    page->last_used_frame = renderer->frame_id;
    *out_page = best_page;
    *out_offset = best_offset;
    return true;
}

static bool nc__renderer_extension_list_contains(
    const VkExtensionProperties* extensions,
    const uint32_t extension_count,
    const char* const name
) {
    for (uint32_t i = 0; i < extension_count; i++) {
        if (strcmp(extensions[i].extensionName, name) == 0) {
            return true;
        }
    }

    return false;
}

static bool nc__renderer_format_supports_optimal_features(
    VkPhysicalDevice physical_device,
    const VkFormat format,
    const VkFormatFeatureFlags required_features
) {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);
    return (properties.optimalTilingFeatures & required_features) == required_features;
}

static VkCompositeAlphaFlagBitsKHR nc__renderer_choose_composite_alpha(const VkCompositeAlphaFlagsKHR supported) {
    static const VkCompositeAlphaFlagBitsKHR preferences[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (uint32_t i = 0; i < NC_COUNTOF(preferences); i++) {
        if (supported & preferences[i]) {
            return preferences[i];
        }
    }

    // This must never happen per the spec.
    NC_ASSERT(false);
    return (VkCompositeAlphaFlagBitsKHR)0;
}

static bool nc__renderer_find_required_extensions(
    VkPhysicalDevice physical_device,
    const char** out_enabled_extensions,
    bool* khr_get_buffer_device_address
) {
    VkExtensionProperties* extensions = NULL;

    uint32_t extension_count;
    NC__CHECK_VK_RESULT(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &extension_count, NULL));

    extensions = malloc(extension_count * sizeof(*extensions));
    NC__CHECK_VK_RESULT(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &extension_count, extensions));

    uint32_t enabled_extension_count = 0;
    for (uint32_t i = 0; i < NC__RENDERER_REQUIRED_EXTENSION_COUNT; i++) {
        // iterate required extensions
        const nc__renderer_required_extension* required_extension = &nc__renderer_required_extensions[i];
        const char* selected_name = NULL;
        for (uint32_t j = 0; required_extension->alternative_names[j]; j++) {
            // iterate variants of the required extension
            const char* const name = required_extension->alternative_names[j];
            if (nc__renderer_extension_list_contains(extensions, extension_count, name)) {
                // check the variant against the device's extension list
                selected_name = name;
                break;
            }
        }

        if (!selected_name) {
            free(extensions);
            return false;
        }

        out_enabled_extensions[enabled_extension_count++] = selected_name;
        if (strcmp(selected_name, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0) {
            *khr_get_buffer_device_address = true;
        } else if (strcmp(selected_name, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0) {
            *khr_get_buffer_device_address = false;
        }
    }

    free(extensions);
    return enabled_extension_count == NC__RENDERER_REQUIRED_EXTENSION_COUNT;

error:
    free(extensions);
    return false;
}

static bool nc__renderer_physical_device_supports_required_features(
    const nc_renderer_t* renderer,
    VkPhysicalDevice physical_device,
    const VkPhysicalDeviceProperties* properties,
    const bool khr_get_buffer_device_address
) {
    if (properties->apiVersion < NC__RENDERER_VK_API_VERSION) {
        return false;
    }

    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
    };
    VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_features_ext = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT,
    };
    VkPhysicalDeviceScalarBlockLayoutFeaturesEXT scalar_block_layout_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &scalar_block_layout_features,
    };
    if (khr_get_buffer_device_address) {
        scalar_block_layout_features.pNext = &buffer_device_address_features;
    } else {
        scalar_block_layout_features.pNext = &buffer_device_address_features_ext;
    }

    vkGetPhysicalDeviceFeatures2(physical_device, &features);

    VkSurfaceCapabilitiesKHR surface_capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physical_device,
            renderer->surface,
            &surface_capabilities) != VK_SUCCESS) {
        return false;
    }

    if (       !features.features.independentBlend
            || !(surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            || !nc__renderer_choose_composite_alpha(surface_capabilities.supportedCompositeAlpha)) {
        return false;
    }

    const VkFormatFeatureFlags oit_format_features =
              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
            | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if (!nc__renderer_format_supports_optimal_features(
                physical_device,
                NC__RENDERER_DEPTH_FORMAT,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            || !nc__renderer_format_supports_optimal_features(
                    physical_device,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    oit_format_features)
            || !nc__renderer_format_supports_optimal_features(
                    physical_device,
                    VK_FORMAT_R16_SFLOAT,
                    oit_format_features)) {
        return false;
    }

#ifdef ANDROID
    if (!features.features.textureCompressionASTC_LDR) {
        return false;
    }
#else
    if (!features.features.textureCompressionBC) {
        return false;
    }
#endif

    const VkBool32 buffer_device_address = khr_get_buffer_device_address
            ? buffer_device_address_features.bufferDeviceAddress
            : buffer_device_address_features_ext.bufferDeviceAddress;
    if (!buffer_device_address || !scalar_block_layout_features.scalarBlockLayout) {
        return false;
    }

    return true;
}

static bool nc__renderer_find_queue_family(
    const nc_renderer_t* renderer,
    VkPhysicalDevice physical_device,
    uint32_t* out_queue_family_index
) {
    VkQueueFamilyProperties* queue_families = NULL;

    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);

    queue_families = malloc(queue_family_count * sizeof(*queue_families));
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; i++) {
        VkBool32 present_supported;
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

VkDeviceSize nc__renderer_get_vram_size(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    VkDeviceSize total = 0;
    for (uint32_t i = 0; i < memory_properties.memoryHeapCount; ++i) {
        const VkMemoryHeap heap = memory_properties.memoryHeaps[i];

        // Check whether the heap is dedicated memory.
        if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            total += heap.size;
        }
    }

    return total;
}

static bool nc__renderer_select_physical_device(
    nc_renderer_t* renderer,
    const bool prefer_low_power,
    const char* enabled_device_extensions[NC__RENDERER_REQUIRED_EXTENSION_COUNT]
) {
    VkPhysicalDevice* physical_devices = NULL;

    uint32_t physical_device_count;
    NC__CHECK_VK_RESULT(vkEnumeratePhysicalDevices(renderer->instance, &physical_device_count, NULL));
    NC_CHECK_RESULT(physical_device_count > 0, "No Vulkan physical devices were found.");

    physical_devices = malloc(physical_device_count * sizeof(*physical_devices));
    NC__CHECK_VK_RESULT(vkEnumeratePhysicalDevices(renderer->instance, &physical_device_count, physical_devices));

    const int selected_gpu = nc_cvar_get_selected_gpu();
    const nc_gpu_memory_preference_t gpu_memory_preference = nc_cvar_get_gpu_memory_preference();

    int highest_score = 0;
    VkDeviceSize best_memory = 0;
    for (uint32_t i = 0; i < physical_device_count; i++) {
        uint32_t queue_family_index;
        bool khr_get_buffer_device_address = false;
        const char* candidate_device_extensions[NC__RENDERER_REQUIRED_EXTENSION_COUNT];

        VkPhysicalDeviceProperties physical_device_properties;
        vkGetPhysicalDeviceProperties(physical_devices[i], &physical_device_properties);

        if (!nc__renderer_find_required_extensions(
                physical_devices[i],
                candidate_device_extensions,
                &khr_get_buffer_device_address)
            || !nc__renderer_physical_device_supports_required_features(
                    renderer,
                    physical_devices[i],
                    &physical_device_properties,
                    khr_get_buffer_device_address)
            || !nc__renderer_find_queue_family(renderer, physical_devices[i], &queue_family_index)) {
            continue;
        }

        if (selected_gpu == (int)i) {
            // Insuperable GPU.
            highest_score = 999;
            renderer->physical_device = physical_devices[i];
            renderer->queue_family_index = queue_family_index;
            renderer->physical_device_properties = physical_device_properties;
            renderer->khr_get_buffer_device_address = khr_get_buffer_device_address;
            memcpy(enabled_device_extensions, candidate_device_extensions, sizeof(candidate_device_extensions));
            break;
        }

        int current_score;
        switch (physical_device_properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_OTHER:
                current_score = 1;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                current_score = prefer_low_power ? 5 : 4;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                current_score = prefer_low_power ? 4 : 5;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                current_score = 3;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                current_score = 2;
                break;
            default:
                current_score = 0;
                continue;
        }

        if (current_score > highest_score) {
            // Prioritize a GPU of the preferred type.
            highest_score = current_score;
            renderer->physical_device = physical_devices[i];
            renderer->queue_family_index = queue_family_index;
            renderer->physical_device_properties = physical_device_properties;
            renderer->khr_get_buffer_device_address = khr_get_buffer_device_address;
            memcpy(enabled_device_extensions, candidate_device_extensions, sizeof(candidate_device_extensions));
            best_memory = nc__renderer_get_vram_size(physical_devices[i]);
        } else if (gpu_memory_preference != NC_GPU_MEMORY_PREFERENCE_NONE && current_score == highest_score) {
            const VkDeviceSize current_memory = nc__renderer_get_vram_size(physical_devices[i]);
            const bool larger = current_memory > best_memory;
            const bool smaller = current_memory < best_memory;
            const bool preferred =
                    (larger && gpu_memory_preference == NC_GPU_MEMORY_PREFERENCE_LARGER)
                    || (smaller && gpu_memory_preference == NC_GPU_MEMORY_PREFERENCE_SMALLER);

            if (preferred) {
                renderer->physical_device = physical_devices[i];
                renderer->queue_family_index = queue_family_index;
                renderer->physical_device_properties = physical_device_properties;
                renderer->khr_get_buffer_device_address = khr_get_buffer_device_address;
                memcpy(enabled_device_extensions, candidate_device_extensions, sizeof(candidate_device_extensions));
                best_memory = current_memory;
            }
        }
    }

    if (highest_score == 0) {
        NC_SET_ERROR("No suitable Vulkan physical device was found.");
        goto error;
    }

    free((void*)physical_devices);
    return true;

error:
    free((void*)physical_devices);
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

static bool nc__renderer_create_device(
    nc_renderer_t* renderer,
    const char* enabled_device_extensions[NC__RENDERER_REQUIRED_EXTENSION_COUNT]
) {
    VkPhysicalDeviceFeatures2 enabled_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = {
            .independentBlend = VK_TRUE,
        },
    };
    VkPhysicalDeviceScalarBlockLayoutFeaturesEXT scalar_block_layout_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT,
        .scalarBlockLayout = VK_TRUE,
    };
    enabled_features.pNext = &scalar_block_layout_features;
#ifdef ANDROID
    enabled_features.features.textureCompressionASTC_LDR = VK_TRUE;
#else
    enabled_features.features.textureCompressionBC = VK_TRUE;
#endif
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_features_ext = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT,
        .bufferDeviceAddress = VK_TRUE,
    };

    scalar_block_layout_features.pNext = renderer->khr_get_buffer_device_address
            ? (void*)&buffer_device_address_features
            : (void*)&buffer_device_address_features_ext;

    NC__CHECK_VK_RESULT(vkCreateDevice(
            renderer->physical_device,
            &(VkDeviceCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .pNext = &enabled_features,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = renderer->queue_family_index,
                    .queueCount = 1,
                    .pQueuePriorities = &(float){ 1.0f },
                },
                .enabledExtensionCount = NC__RENDERER_REQUIRED_EXTENSION_COUNT,
                .ppEnabledExtensionNames = enabled_device_extensions,
            },
            NULL,
            &renderer->device));
    volkLoadDevice(renderer->device);
    renderer->get_buffer_device_address = renderer->khr_get_buffer_device_address
            ? vkGetBufferDeviceAddressKHR
            : vkGetBufferDeviceAddressEXT;
    if (!renderer->get_buffer_device_address) {
        // last attempt at getting this function pointer
        renderer->get_buffer_device_address = vkGetBufferDeviceAddress;
    }
    vkGetDeviceQueue(renderer->device, renderer->queue_family_index, 0, &renderer->queue);

    NC__CHECK_VK_RESULT(vmaCreateAllocator(
            &(VmaAllocatorCreateInfo){
                .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
                .physicalDevice = renderer->physical_device,
                .device = renderer->device,
                .instance = renderer->instance,
                .vulkanApiVersion = NC__RENDERER_VK_API_VERSION,
                .pVulkanFunctions = &(VmaVulkanFunctions){
                    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
                    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
                },
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
        const SDL_DisplayID display_id = SDL_GetDisplayForWindow(renderer->window);
        NC_CHECK_SDL_RESULT(display_id);
        SDL_DisplayMode display_mode;
        bool sdl_result = SDL_GetClosestFullscreenDisplayMode(
                display_id,
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
    renderer->swapchain_extent.x = (uint16_t)width;
    renderer->swapchain_extent.y = (uint16_t)height;
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
            .format = NC__RENDERER_PREFERRED_SWAPCHAIN_FORMAT,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
        return true;
    }

    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == NC__RENDERER_PREFERRED_SWAPCHAIN_FORMAT &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *out_format = formats[i];
            return true;
        }
    }

    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == NC__RENDERER_ALTERNATIVE_SWAPCHAIN_FORMAT &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *out_format = formats[i];
            return true;
        }
    }

    NC_SET_ERROR("The Vulkan surface has no supported sRGB swapchain format.");
    goto error;

error:
    return false;
}

static bool nc__renderer_get_swapchain_extent(
    const nc_renderer_t* renderer,
    const VkSurfaceCapabilitiesKHR* capabilities,
    VkExtent2D* out_extent
) {
#ifndef ANDROID
    if (capabilities->currentExtent.width != UINT32_MAX) {
        *out_extent = capabilities->currentExtent;
        return out_extent->width != 0 && out_extent->height != 0;
    }
#endif

    int width;
    int height;
    const bool sdl_result = SDL_GetWindowSizeInPixels(renderer->window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);

    if (width <= 0 || height <= 0) {
        return false;
    }

    // Android permits swapchain images to differ from currentExtent and scales them for
    // presentation. Use SDL's current drawable size.
    if (nc__renderer_surface_transform_swaps_extent(capabilities->currentTransform)) {
        const int display_width = width;
        width = height;
        height = display_width;
    }

    out_extent->width = vkm_clamp(
            (uint32_t)width,
            capabilities->minImageExtent.width,
            capabilities->maxImageExtent.width);
    out_extent->height = vkm_clamp(
            (uint32_t)height,
            capabilities->minImageExtent.height,
            capabilities->maxImageExtent.height);
    return out_extent->width != 0 && out_extent->height != 0;

error:
    return false;
}

static bool nc__renderer_create_render_pass(nc_renderer_t* renderer) {
    // Opaque geometry writes depth, but the later OIT and composition subpasses only need to retain the same depth
    // attachment. Making it read-only after the opaque pass helps Arm GPUs keep early ZS testing enabled and allows
    // the driver to fuse all subpasses into one physical tile pass.
    NC__CHECK_VK_RESULT(vkCreateRenderPass(
            renderer->device,
            &(VkRenderPassCreateInfo){
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = 4,
                .pAttachments = (VkAttachmentDescription[]){
                    {
                        .format = renderer->swapchain_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        // PRESENT and COLOR_ATTACHMENT are both transaction-elimination-safe layouts on Arm. Keeping
                        // the swapchain image in safe layouts preserves its tile-signature metadata between frames.
                        .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    },
                    {
                        .format = NC__RENDERER_DEPTH_FORMAT,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        // These steady-state layouts match the preceding frame. A one-time barrier establishes them
                        // after image creation, so subsequent render passes need no external attachment barriers.
                        .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    },
                    {
                        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        // The OIT intermediates remain in safe layouts for their entire lifetime. DONT_CARE stores and
                        // transient lazy memory allow a tile renderer to discard their contents without RAM traffic.
                        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    },
                    {
                        .format = VK_FORMAT_R16_SFLOAT,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        // See the accumulation attachment above. Both intermediates are consumed as input attachments
                        // inside this render pass instead of being stored and sampled through external memory.
                        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    },
                },
                .subpassCount = 3,
                .pSubpasses = (VkSubpassDescription[]){
                    // opaque pass
                    {
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &(VkAttachmentReference){
                            .attachment = 0,
                            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        },
                        .pDepthStencilAttachment = &(VkAttachmentReference){
                            .attachment = 1,
                            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        },
                    },
                    // OIT accumulation + reveal pass
                    {
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .colorAttachmentCount = 2,
                        .pColorAttachments = (VkAttachmentReference[]){
                            {
                                .attachment = 2,
                                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            },
                            {
                                .attachment = 3,
                                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            },
                        },
                        .pDepthStencilAttachment = &(VkAttachmentReference){
                            .attachment = 1,
                            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        },
                        .preserveAttachmentCount = 1,
                        .pPreserveAttachments = &(uint32_t){ 0 },
                    },
                    // composition pass
                    {
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .inputAttachmentCount = 2,
                        .pInputAttachments = (VkAttachmentReference[]){
                            {
                                .attachment = 2,
                                // SHADER_READ_ONLY is an Arm transaction-elimination-safe layout and enables a
                                // tile-local input-attachment read when the subpasses are fused.
                                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            },
                            {
                                .attachment = 3,
                                // Keep reveal tile-local for the same reason as accumulation above.
                                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            },
                        },
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &(VkAttachmentReference){
                            .attachment = 0,
                            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        },
                        .pDepthStencilAttachment = &(VkAttachmentReference){
                            .attachment = 1,
                            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        },
                    },
                },
                // All dependencies are framebuffer-local. This is the key signal that lets a tile renderer keep the
                // intermediate values on-chip while progressing through the subpasses tile by tile.
                .dependencyCount = 3,
                .pDependencies = (VkSubpassDependency[]){
                    {
                        .srcSubpass = VK_SUBPASS_EXTERNAL,
                        .dstSubpass = 0,
                        .srcStageMask =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .dstStageMask =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .dstAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        // BY_REGION expresses a per-pixel dependency, allowing tile-based GPUs to advance one tile
                        // through the subpasses without waiting for the entire framebuffer.
                        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                    },
                    {
                        .srcSubpass = 0,
                        .dstSubpass = 1,
                        .srcStageMask =
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        .dstStageMask =
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                                | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        .dstAccessMask =
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                    },
                    {
                        .srcSubpass = 1,
                        .dstSubpass = 2,
                        .srcStageMask =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        .dstStageMask =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        .srcAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                        // DONT_CARE store operations are still writes for synchronization purposes. Include the color
                        // and depth store accesses in the last subpass so they happen after the implicit transitions
                        // that make its input and read-only depth layouts available.
                        .dstAccessMask =
                                  VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
                                | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                    },
                },
            },
            NULL,
            &renderer->render_pass));
    return true;

error:
    return false;
}

static void nc__renderer_destroy_image(nc_renderer_t* renderer, nc__renderer_image_t* image) {
    vkDestroyImageView(renderer->device, image->view, NULL);

    if (image->image) {
        vmaDestroyImage(renderer->allocator, image->image, image->allocation);
    }

    *image = (nc__renderer_image_t){ 0 };
}

static bool nc__renderer_create_render_pass_attachment(
    nc_renderer_t* renderer,
    nc__renderer_image_t* image,
    const VkFormat format,
    const bool color_and_input
) {
    nc__renderer_destroy_image(renderer, image);

    NC__CHECK_VK_RESULT(vmaCreateDedicatedImage(
            renderer->allocator,
            &(VkImageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = format,
                .extent = { renderer->swapchain_extent.x, renderer->swapchain_extent.y, 1, },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = (
                    color_and_input
                        ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
                        : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                    | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                // must be UNDEFINED or PREINITIALIZED or ZERO_INITIALIZED
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
            &(VmaAllocationCreateInfo){
                .preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
            },
            NULL,
            &image->image,
            &image->allocation,
            NULL));

    NC__CHECK_VK_RESULT(vkCreateImageView(
            renderer->device,
            &(VkImageViewCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image->image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = format,
                .subresourceRange = {
                    .aspectMask = color_and_input ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
            NULL,
            &image->view));
    return true;

error:
    nc__renderer_destroy_image(renderer, image);
    return false;
}

static void nc__renderer_initialize_render_pass_attachments(nc_renderer_t* renderer) {
    // These newly created optimal-tiled images start as UNDEFINED, while the steady-state render pass deliberately
    // starts from safe layouts. Establish those layouts once after each swapchain recreation; the render pass then
    // performs all recurring subpass and final transitions implicitly.
    // This exists for the transaction elimination ARM optimization.
    if (!renderer->render_pass_attachments_initialized) {
        const VkImageMemoryBarrier barriers[] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = renderer->depth.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = renderer->accumulation.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = renderer->reveal.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
        };
        vkCmdPipelineBarrier(
                renderer->frame_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0,
                NULL,
                0,
                NULL,
                NC_COUNTOF(barriers),
                barriers);
        renderer->render_pass_attachments_initialized = true;
    }

    const uint32_t image_index = renderer->frame_swapchain_image_index;
    if (!renderer->swapchain_image_initialized[image_index]) {
        // Each swapchain image is UNDEFINED until its first acquisition. Move it to PRESENT once so every later frame
        // can follow the transaction-elimination-safe PRESENT -> COLOR_ATTACHMENT -> PRESENT render-pass path. The
        // acquired-image semaphore wait includes COLOR_ATTACHMENT_OUTPUT, so this transition cannot race presentation.
        const VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = renderer->swapchain_images[image_index],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        vkCmdPipelineBarrier(
                renderer->frame_command_buffer,
                // The acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT. Put the transition's source scope at
                // that stage too, otherwise it could execute before the swapchain image has actually been acquired.
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0,
                NULL,
                0,
                NULL,
                1,
                &barrier);
        renderer->swapchain_image_initialized[image_index] = true;
    }
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
            renderer->depth.view,
            renderer->accumulation.view,
            renderer->reveal.view,
        };
        NC__CHECK_VK_RESULT(vkCreateFramebuffer(
                renderer->device,
                &(VkFramebufferCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                    .renderPass = renderer->render_pass,
                    .attachmentCount = NC_COUNTOF(attachments),
                    .pAttachments = attachments,
                    .width = renderer->swapchain_extent.x,
                    .height = renderer->swapchain_extent.y,
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

static void nc__renderer_destroy_present_semaphore_array(
    const nc_renderer_t* renderer,
    VkSemaphore* semaphores,
    const uint32_t semaphore_count
) {
    if (!semaphores) {
        return;
    }

    for (uint32_t i = 0; i < semaphore_count; i++) {
        vkDestroySemaphore(renderer->device, semaphores[i], NULL);
    }
    free(semaphores);
}

static void nc__renderer_destroy_present_semaphores(nc_renderer_t* renderer) {
    nc__renderer_destroy_present_semaphore_array(
            renderer,
            renderer->render_finished_semaphores,
            renderer->swapchain_image_count);
    renderer->render_finished_semaphores = NULL;
}

static void nc__renderer_destroy_retired_swapchains(nc_renderer_t* renderer) {
    for (uint32_t i = 0; i < nc__renderer_retired_swapchain_vec_count(&renderer->retired_swapchains); i++) {
        const nc__renderer_retired_swapchain_t retired = nc__renderer_retired_swapchain_vec_get(
                &renderer->retired_swapchains,
                i);
        nc__renderer_destroy_present_semaphore_array(
                renderer,
                retired.present_semaphores,
                retired.present_semaphore_count);
        vkDestroySwapchainKHR(renderer->device, retired.swapchain, NULL);
    }
    nc__renderer_retired_swapchain_vec_clear(&renderer->retired_swapchains);
}

static VkSwapchainKHR nc__renderer_retire_swapchain(nc_renderer_t* renderer) {
    const VkSwapchainKHR swapchain = renderer->swapchain;
    if (!swapchain) {
        return VK_NULL_HANDLE;
    }

    nc__renderer_retired_swapchain_vec_append(
            &renderer->retired_swapchains,
            (nc__renderer_retired_swapchain_t){
                .swapchain = swapchain,
                .present_semaphores = renderer->render_finished_semaphores,
                .present_semaphore_count = renderer->swapchain_image_count,
            });
    renderer->swapchain = VK_NULL_HANDLE;
    renderer->render_finished_semaphores = NULL;
    return swapchain;
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

static void nc__renderer_destroy_swapchain_images(nc_renderer_t* renderer) {
    nc__renderer_destroy_framebuffers(renderer);
    nc__renderer_destroy_image(renderer, &renderer->depth);
    nc__renderer_destroy_image(renderer, &renderer->accumulation);
    nc__renderer_destroy_image(renderer, &renderer->reveal);
    // Newly created replacement images return to UNDEFINED and need their one-time initialization barriers again.
    renderer->render_pass_attachments_initialized = false;
    nc__renderer_destroy_present_semaphores(renderer);

    if (renderer->swapchain_image_views) {
        for (uint32_t i = 0; i < renderer->swapchain_image_count; i++) {
            vkDestroyImageView(renderer->device, renderer->swapchain_image_views[i], NULL);
        }
        free(renderer->swapchain_image_views);
        renderer->swapchain_image_views = NULL;
    }

    free((void*)renderer->swapchain_images);
    renderer->swapchain_images = NULL;
    free(renderer->swapchain_image_initialized);
    renderer->swapchain_image_initialized = NULL;
    renderer->swapchain_image_count = 0;
}

static void nc__renderer_destroy_current_swapchain(nc_renderer_t* renderer) {
    nc__renderer_destroy_swapchain_images(renderer);

    if (renderer->swapchain) {
        vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
        renderer->swapchain = VK_NULL_HANDLE;
    }
}

static void nc__renderer_destroy_swapchain(nc_renderer_t* renderer) {
    nc__renderer_destroy_current_swapchain(renderer);
    nc__renderer_destroy_retired_swapchains(renderer);
    renderer->frame_completes_present_retirement = false;
    renderer->retired_swapchains_pending_destruction = false;
    renderer->swapchain_retirement_probe_presented = false;
}

static bool nc__renderer_create_swapchain(nc_renderer_t* renderer, const VkSwapchainKHR old_swapchain) {
    VkSurfaceFormatKHR* formats = NULL;
    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    VkSurfaceCapabilitiesKHR capabilities;
    NC__CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            renderer->physical_device,
            renderer->surface,
            &capabilities));
    renderer->surface_transform = capabilities.currentTransform;

    NC_ASSERT(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

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
    NC_ASSERT(nc__renderer_format_supports_optimal_features(
            renderer->physical_device,
            chosen_format.format,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT));

    VkExtent2D extent;
    if (!nc__renderer_get_swapchain_extent(renderer, &capabilities, &extent)) {
        free(formats);
        return true;
    }

    if (renderer->render_pass && renderer->swapchain_format != chosen_format.format) {
        NC_SET_ERROR("The swapchain format changed after renderer initialization.");
        goto error;
    }

    const VkCompositeAlphaFlagBitsKHR composite_alpha = nc__renderer_choose_composite_alpha(
            capabilities.supportedCompositeAlpha);
    NC_ASSERT(composite_alpha);
    // The spec states that a max image count of 0 means unlimited. We only want 3.
    const uint32_t max_image_count = capabilities.maxImageCount ? capabilities.maxImageCount : 3;
    // Ask for triple buffering.
    const uint32_t image_count = vkm_clamp(3, capabilities.minImageCount, max_image_count);
    // TODO: This occasionally returns the surface lost error when switching apps.
    const VkResult create_result = vkCreateSwapchainKHR(
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
                .preTransform = renderer->surface_transform,
                .compositeAlpha = composite_alpha,
                .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                .clipped = VK_TRUE,
                .oldSwapchain = old_swapchain,
            },
            NULL,
            &new_swapchain);
    NC__CHECK_VK_RESULT(create_result);

    renderer->swapchain = new_swapchain;
    renderer->swapchain_retirement_probe_presented = false;
    renderer->swapchain_format = chosen_format.format;
    renderer->swapchain_extent.x = (uint16_t)extent.width;
    renderer->swapchain_extent.y = (uint16_t)extent.height;

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
    renderer->swapchain_image_initialized = calloc(
            renderer->swapchain_image_count,
            sizeof(*renderer->swapchain_image_initialized));

    renderer->swapchain_image_views = (VkImageView*)calloc(
            renderer->swapchain_image_count,
            sizeof(*renderer->swapchain_image_views));
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
    if (       !nc__renderer_create_present_semaphores(renderer)
            || !nc__renderer_create_render_pass_attachment(
                    renderer,
                    &renderer->depth,
                    NC__RENDERER_DEPTH_FORMAT,
                    false)
            || !nc__renderer_create_render_pass_attachment(
                    renderer,
                    &renderer->accumulation,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    true)
            || !nc__renderer_create_render_pass_attachment(
                    renderer,
                    &renderer->reveal,
                    VK_FORMAT_R16_SFLOAT,
                    true)
            || !nc__renderer_create_framebuffers(renderer)) {
        goto error;
    }

    free(formats);
    renderer->swapchain_dirty = false;
    renderer->surface_dirty = false;
    return true;

error:
    free(formats);
    // This replacement was never presented, so it is safe to destroy. Older generations still need their acquire
    // proof and must remain in retired_swapchains.
    nc__renderer_destroy_current_swapchain(renderer);
    return false;
}

static bool nc__renderer_recreate_swapchain(nc_renderer_t* renderer) {
    // begin_frame has waited for our sole submission fence, so framebuffers, attachment images, and image views are
    // no longer in use. Presentation is not covered by that fence; its swapchain and semaphores are retired below.
    NC_ASSERT(!renderer->frame_fence_pending);

    if (renderer->surface_dirty) {
        // Core Vulkan has no strict completion primitive for presentation on a lost surface. As at shutdown, device
        // idle is the pragmatic extension-free fallback before destroying the old surface and its swapchains.
        nc__renderer_wait_idle(renderer);
        nc__renderer_destroy_swapchain(renderer);
        if (!nc__renderer_create_surface(renderer)) {
            return false;
        }
        return nc__renderer_create_swapchain(renderer, VK_NULL_HANDLE);
    }

    const VkSwapchainKHR old_swapchain = nc__renderer_retire_swapchain(renderer);
    nc__renderer_destroy_swapchain_images(renderer);
    return nc__renderer_create_swapchain(renderer, old_swapchain);
}

static bool nc__renderer_create_descriptor_pool(nc_renderer_t* renderer) {
    NC__CHECK_VK_RESULT(vkCreateDescriptorPool(
            renderer->device,
            &(VkDescriptorPoolCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = 0,
                .maxSets = NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
                .poolSizeCount = 2,
                .pPoolSizes = (VkDescriptorPoolSize[]){
                    {
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
                    },
                    {
                        .type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                        .descriptorCount = 2 * NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS,
                    },
                },
            },
            NULL,
            &renderer->frame_descriptor_pool));
    return true;

error:
    return false;
}

static bool nc__renderer_create_descriptor_set_layouts(nc_renderer_t* renderer) {
    NC__CHECK_VK_RESULT(vkCreateDescriptorSetLayout(
            renderer->device,
            &(VkDescriptorSetLayoutCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &(VkDescriptorSetLayoutBinding){
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
            NULL,
            &renderer->texture_descriptor_set_layout));
    NC__CHECK_VK_RESULT(vkCreateDescriptorSetLayout(
            renderer->device,
            &(VkDescriptorSetLayoutCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 2,
                .pBindings = (VkDescriptorSetLayoutBinding[]){
                    {
                        .binding = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    {
                        .binding = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                },
            },
            NULL,
            &renderer->composite_descriptor_set_layout));
    NC__CHECK_VK_RESULT(vkCreatePipelineLayout(
            renderer->device,
            &(VkPipelineLayoutCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &renderer->texture_descriptor_set_layout,
                .pushConstantRangeCount = 2,
                .pPushConstantRanges = (VkPushConstantRange[]){
                    {
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                        .offset = NC__RENDERER_VERTEX_PUSH_CONSTANT_OFFSET,
                        .size = 2 * sizeof(VkDeviceAddress),
                    },
                    {
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .offset = sizeof(VkDeviceAddress),
                        .size = 2 * sizeof(VkDeviceAddress),
                    },
                },
            },
            NULL,
            &renderer->pipeline_layout));
    NC__CHECK_VK_RESULT(vkCreatePipelineLayout(
            renderer->device,
            &(VkPipelineLayoutCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &renderer->composite_descriptor_set_layout,
            },
            NULL,
            &renderer->composite_pipeline_layout));
    return true;

error:
    return false;
}

static void nc__renderer_destroy_descriptor_state(nc_renderer_t* renderer) {
    vkDestroyDescriptorPool(renderer->device, renderer->frame_descriptor_pool, NULL);
    renderer->frame_descriptor_pool = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(renderer->device, renderer->pipeline_layout, NULL);
    renderer->pipeline_layout = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(renderer->device, renderer->composite_pipeline_layout, NULL);
    renderer->composite_pipeline_layout = VK_NULL_HANDLE;

    vkDestroyDescriptorSetLayout(renderer->device, renderer->texture_descriptor_set_layout, NULL);
    renderer->texture_descriptor_set_layout = VK_NULL_HANDLE;

    vkDestroyDescriptorSetLayout(renderer->device, renderer->composite_descriptor_set_layout, NULL);
    renderer->composite_descriptor_set_layout = VK_NULL_HANDLE;
}

static VkShaderModule nc__renderer_load_shader(
    const nc_renderer_t* renderer,
    nc_asset_manager_t* asset_manager,
    const char* name,
    const nc_shader_stage_t stage
) {
    nc_shader_baked_asset_t asset;
    if (!nc_asset_manager_get_shader_baked_asset(asset_manager, "novacube", name, stage, &asset)) {
        return VK_NULL_HANDLE;
    }

    VkShaderModule shader_module = VK_NULL_HANDLE;
    NC__CHECK_VK_RESULT(vkCreateShaderModule(
            renderer->device,
            &(VkShaderModuleCreateInfo){
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = asset.code_size,
                .pCode = asset.spirv_bytecode,
            },
            NULL,
            &shader_module));

error:
    nc_asset_manager_shader_baked_asset_fini(&asset);
    return shader_module;
}

static bool nc__renderer_create_graphics_pipeline(
    const nc_renderer_t* renderer,
    nc_asset_manager_t* asset_manager,
    const VkPipelineLayout pipeline_layout,
    const char* vertex_shader_name,
    const char* fragment_shader_name,
    const VkPrimitiveTopology topology,
    const VkPipelineVertexInputStateCreateInfo* vertex_input_state,
    const VkPipelineRasterizationStateCreateInfo* rasterization_state,
    const VkPipelineDepthStencilStateCreateInfo* depth_stencil_state,
    const uint32_t color_blend_attachment_count,
    const VkPipelineColorBlendAttachmentState* color_blend_attachments,
    const uint32_t subpass,
    VkPipeline* out_pipeline
) {
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;

    vertex_shader = nc__renderer_load_shader(
            renderer,
            asset_manager,
            vertex_shader_name,
            NC_SHADER_STAGE_VERTEX);
    if (!vertex_shader) {
        goto error;
    }

    fragment_shader = nc__renderer_load_shader(
            renderer,
            asset_manager,
            fragment_shader_name,
            NC_SHADER_STAGE_FRAGMENT);
    if (!fragment_shader) {
        goto error;
    }

    NC__CHECK_VK_RESULT(vkCreateGraphicsPipelines(
            renderer->device,
            VK_NULL_HANDLE,
            1,
            &(VkGraphicsPipelineCreateInfo){
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = (VkPipelineShaderStageCreateInfo[]){
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
                },
                .pVertexInputState = vertex_input_state,
                .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                    .topology = topology,
                },
                .pViewportState = &(VkPipelineViewportStateCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                    .viewportCount = 1,
                    .scissorCount = 1,
                },
                .pRasterizationState = rasterization_state,
                .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                },
                .pDepthStencilState = depth_stencil_state,
                .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                    .attachmentCount = color_blend_attachment_count,
                    .pAttachments = color_blend_attachments,
                },
                .pDynamicState = &(VkPipelineDynamicStateCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                    .dynamicStateCount = 2,
                    .pDynamicStates = (VkDynamicState[]){
                        VK_DYNAMIC_STATE_VIEWPORT,
                        VK_DYNAMIC_STATE_SCISSOR,
                    },
                },
                .layout = pipeline_layout,
                .renderPass = renderer->render_pass,
                .subpass = subpass,
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

static bool nc__renderer_create_pipelines(nc_renderer_t* renderer, nc_asset_manager_t* asset_manager) {
    const VkPipelineVertexInputStateCreateInfo no_vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineRasterizationStateCreateInfo raster_no_cull = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .lineWidth = 1.0f,
    };
    const VkPipelineRasterizationStateCreateInfo raster_back_cull = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
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
    const VkPipelineDepthStencilStateCreateInfo depth_sky = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,

    };
    const VkPipelineColorBlendAttachmentState no_blending = {
        .colorWriteMask =
                  VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendAttachmentState alpha_blending = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
                  VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendAttachmentState subtract_blending = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_SUBTRACT,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
                  VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendAttachmentState additive_blending = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
                  VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendAttachmentState multiplicative_blending = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
                  VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT,
    };

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "chunk",
            "chunk_opaque",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            &no_vertex_input,
            &raster_back_cull,
            &depth_enabled_write,
            1,
            &no_blending,
            0,
            &renderer->opaque_chunk_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "chunk",
            "chunk_transparent",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            &no_vertex_input,
            &raster_no_cull,
            &depth_enabled_no_write,
            2,
            (VkPipelineColorBlendAttachmentState[]){ additive_blending, multiplicative_blending },
            1,
            &renderer->transparent_chunk_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->composite_pipeline_layout,
            "fullscreen_triangle",
            "chunk_composite",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            &no_vertex_input,
            &raster_back_cull,
            &depth_disabled,
            1,
            &alpha_blending,
            2,
            &renderer->composite_chunk_pipeline)) {
        return false;
    }

    if (!   nc__renderer_create_graphics_pipeline(
                renderer,
                asset_manager,
                renderer->pipeline_layout,
                "block_highlight",
                "block_highlight_outline",
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                &no_vertex_input,
                &raster_back_cull,
                &depth_enabled_no_write,
                1,
                &alpha_blending,
                2,
                &renderer->outline_block_highlight_pipeline)
         || !nc__renderer_create_graphics_pipeline(
                renderer,
                asset_manager,
                renderer->pipeline_layout,
                "block_highlight",
                "block_highlight_vignette",
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                &no_vertex_input,
                &raster_back_cull,
                &depth_enabled_no_write,
                1,
                &alpha_blending,
                2,
                &renderer->vignette_block_highlight_pipeline)
         || !nc__renderer_create_graphics_pipeline(
                renderer,
                asset_manager,
                renderer->pipeline_layout,
                "block_highlight",
                "block_highlight_plasma",
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                &no_vertex_input,
                &raster_back_cull,
                &depth_enabled_no_write,
                1,
                &alpha_blending,
                2,
                &renderer->plasma_block_highlight_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "gui",
            "gui",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            // Transfer pages already expose device addresses; vertex input would require a
            // second upload API that exposes each page's VkBuffer and byte offset.
            &no_vertex_input,
            &raster_no_cull,
            &depth_disabled,
            1,
            &alpha_blending,
            2,
            &renderer->gui_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "gui",
            "gui_image",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            &no_vertex_input,
            &raster_no_cull,
            &depth_disabled,
            1,
            &alpha_blending,
            2,
            &renderer->gui_image_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "fullscreen_triangle",
            "procedural_overlay_invert",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            &no_vertex_input,
            &raster_back_cull,
            &depth_disabled,
            1,
            &subtract_blending,
            2,
            &renderer->procedural_overlay_invert_pipeline)) {
        return false;
    }

    if (!nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "fullscreen_triangle",
            "procedural_overlay_stick",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            &no_vertex_input,
            &raster_back_cull,
            &depth_disabled,
            1,
            &no_blending,
            2,
            &renderer->procedural_overlay_stick_pipeline)) {
        return false;
    }

    return nc__renderer_create_graphics_pipeline(
            renderer,
            asset_manager,
            renderer->pipeline_layout,
            "sky",
            "sky",
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            &no_vertex_input,
            &raster_back_cull,
            &depth_sky,
            1,
            &no_blending,
            0,
            &renderer->sky_pipeline);
}

static void nc__renderer_destroy_pipelines(nc_renderer_t* renderer) {
    vkDestroyPipeline(renderer->device, renderer->procedural_overlay_stick_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->procedural_overlay_invert_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->sky_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->gui_image_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->gui_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->composite_chunk_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->transparent_chunk_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->opaque_chunk_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->plasma_block_highlight_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->vignette_block_highlight_pipeline, NULL);
    vkDestroyPipeline(renderer->device, renderer->outline_block_highlight_pipeline, NULL);

    renderer->procedural_overlay_stick_pipeline = VK_NULL_HANDLE;
    renderer->procedural_overlay_invert_pipeline = VK_NULL_HANDLE;
    renderer->sky_pipeline = VK_NULL_HANDLE;
    renderer->gui_image_pipeline = VK_NULL_HANDLE;
    renderer->gui_pipeline = VK_NULL_HANDLE;
    renderer->composite_chunk_pipeline = VK_NULL_HANDLE;
    renderer->transparent_chunk_pipeline = VK_NULL_HANDLE;
    renderer->opaque_chunk_pipeline = VK_NULL_HANDLE;
    renderer->plasma_block_highlight_pipeline = VK_NULL_HANDLE;
    renderer->vignette_block_highlight_pipeline = VK_NULL_HANDLE;
    renderer->outline_block_highlight_pipeline = VK_NULL_HANDLE;
}

static bool nc__renderer_create_sampler(
    const nc_renderer_t* renderer,
    const VkSamplerMipmapMode mipmap_mode,
    VkSampler* sampler
) {
    NC__CHECK_VK_RESULT(vkCreateSampler(
            renderer->device,
            &(VkSamplerCreateInfo){
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = mipmap_mode,
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

static bool nc__renderer_write_buffer_reference_data(
    nc_renderer_t* renderer,
    const void* data,
    const uint32_t size,
    VkDeviceAddress* out_address
) {
    uint32_t page_index = 0;
    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(
            renderer,
            size,
            NC__RENDERER_BUFFER_REFERENCE_ALIGNMENT,
            &page_index,
            &offset)) {
        return false;
    }
    const nc__renderer_transfer_page_t* page = &renderer->transfer_pages.array[page_index];
    memcpy((uint8_t*)page->mapping + offset, data, size);

    *out_address = page->address + offset;
    return true;
}

static bool nc__renderer_bind_texture_descriptor_set(
    nc_renderer_t* renderer,
    const nc_renderer_texture_t* texture,
    VkSampler sampler
) {
    if (renderer->frame_descriptor_set_count >= NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS) {
        NC_SET_ERROR("The per-frame Vulkan descriptor pool is exhausted.");
        return false;
    }

    VkDescriptorSet texture_set;
    const VkResult result = vkAllocateDescriptorSets(
            renderer->device,
            &(VkDescriptorSetAllocateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = renderer->frame_descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &renderer->texture_descriptor_set_layout,
            },
            &texture_set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        NC_SET_ERROR("The per-frame Vulkan descriptor pool is exhausted.");
        return false;
    }
    NC__CHECK_VK_RESULT(result);

    renderer->frame_descriptor_set_count++;

    vkUpdateDescriptorSets(
            renderer->device,
            1,
            &(VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = texture_set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &(VkDescriptorImageInfo){
                    .sampler = sampler,
                    .imageView = texture->image_view,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                },
            },
            0,
            NULL);

    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout,
            0,
            1,
            &texture_set,
            0,
            NULL);

    return true;

error:
    return false;
}

static bool nc__renderer_bind_composite_descriptor_set(nc_renderer_t* renderer) {
    if (renderer->frame_descriptor_set_count >= NC__RENDERER_MAX_FRAME_DESCRIPTOR_SETS) {
        NC_SET_ERROR("The per-frame Vulkan descriptor pool is exhausted.");
        return false;
    }

    VkDescriptorSet descriptor_set;
    const VkResult result = vkAllocateDescriptorSets(
            renderer->device,
            &(VkDescriptorSetAllocateInfo){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = renderer->frame_descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &renderer->composite_descriptor_set_layout,
            },
            &descriptor_set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        NC_SET_ERROR("The per-frame Vulkan descriptor pool is exhausted.");
        return false;
    }
    NC__CHECK_VK_RESULT(result);

    renderer->frame_descriptor_set_count++;

    const VkDescriptorImageInfo image_infos[] = {
        {
            .imageView = renderer->accumulation.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        {
            .imageView = renderer->reveal.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
    };
    vkUpdateDescriptorSets(
            renderer->device,
            2,
            (VkWriteDescriptorSet[]){
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptor_set,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                    .pImageInfo = &image_infos[0],
                },
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptor_set,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                    .pImageInfo = &image_infos[1],
                },
            },
            0,
            NULL);

    vkCmdBindDescriptorSets(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->composite_pipeline_layout,
            0,
            1,
            &descriptor_set,
            0,
            NULL);
    return true;

error:
    return false;
}

static void nc__renderer_set_viewport_and_scissor(const nc_renderer_t* renderer, const SDL_Rect* scissor_rect) {
    vkCmdSetViewport(
            renderer->frame_command_buffer,
            0,
            1,
            &(VkViewport){
                .x = 0.0f,
                .y = 0.0f,
                .width = (float)renderer->swapchain_extent.x,
                .height = (float)renderer->swapchain_extent.y,
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            });

    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = { renderer->swapchain_extent.x, renderer->swapchain_extent.y },
    };
    if (scissor_rect) {
        SDL_Rect rotated_rect;
        nc__renderer_pre_rotate_rect(renderer, scissor_rect, &rotated_rect);
        scissor.offset.x = rotated_rect.x;
        scissor.offset.y = rotated_rect.y;
        scissor.extent.width = (uint32_t)rotated_rect.w;
        scissor.extent.height = (uint32_t)rotated_rect.h;
    }
    vkCmdSetScissor(renderer->frame_command_buffer, 0, 1, &scissor);
}

static bool nc__renderer_flush_uploads(nc_renderer_t* renderer) {
    if (!renderer->uploads_dirty || nc__renderer_upload_op_vec_count(&renderer->upload_ops) == 0) {
        return true;
    }

    NC_ASSERT(renderer->frame_command_buffer);

    const uint32_t upload_count = nc__renderer_upload_op_vec_count(&renderer->upload_ops);
    uint32_t texture_op_count = 0;
    for (uint32_t i = 0; i < upload_count; i++) {
        const nc__renderer_upload_op_t op = nc__renderer_upload_op_vec_get(&renderer->upload_ops, i);
        texture_op_count += op.kind == NC__RENDERER_UPLOAD_TEXTURE;
    }

    VkImageMemoryBarrier* image_barriers = texture_op_count > 0
            ? calloc(texture_op_count, sizeof(*image_barriers))
            : NULL;
    nc_renderer_texture_t** barrier_textures = texture_op_count > 0
            ? calloc(texture_op_count, sizeof(*barrier_textures))
            : NULL;
    uint32_t texture_barrier_count = 0;
    VkPipelineStageFlags texture_src_stages = 0;

    for (uint32_t i = 0; i < upload_count; i++) {
        const nc__renderer_upload_op_t op = nc__renderer_upload_op_vec_get(&renderer->upload_ops, i);
        if (op.kind != NC__RENDERER_UPLOAD_TEXTURE) {
            continue;
        }

        nc_renderer_texture_t* texture = op.texture.texture;
        bool barrier_already_added = false;
        for (uint32_t j = 0; j < texture_barrier_count; j++) {
            if (barrier_textures[j] == texture) {
                barrier_already_added = true;
                break;
            }
        }
        if (barrier_already_added) {
            continue;
        }

        VkAccessFlags src_access = 0;
        switch (texture->layout) {
            // TODO: Does this affect transaction elimination in ARM?
            case VK_IMAGE_LAYOUT_UNDEFINED:
                texture_src_stages |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                texture_src_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                src_access = VK_ACCESS_SHADER_READ_BIT;
                break;
            default:
                NC_ASSERT(false);
                break;
        }

        barrier_textures[texture_barrier_count] = texture;
        image_barriers[texture_barrier_count] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = src_access,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = texture->layout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = texture->mip_level_count,
                .layerCount = texture->layer_count,
            },
        };
        texture_barrier_count++;
    }

    if (texture_barrier_count > 0) {
        vkCmdPipelineBarrier(
                renderer->frame_command_buffer,
                texture_src_stages,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                NULL,
                0,
                NULL,
                texture_barrier_count,
                image_barriers);
    }

    VkPipelineStageFlags buffer_dst_stages = 0;
    VkAccessFlags buffer_dst_access = 0;

    for (uint32_t i = 0; i < upload_count; i++) {
        const nc__renderer_upload_op_t op = nc__renderer_upload_op_vec_get(&renderer->upload_ops, i);
        NC_ASSERT(op.source_page < nc__renderer_transfer_page_vec_count(&renderer->transfer_pages));
        const nc__renderer_transfer_page_t* source_page = &renderer->transfer_pages.array[op.source_page];
        if (op.kind == NC__RENDERER_UPLOAD_BUFFER) {
            vkCmdCopyBuffer(
                    renderer->frame_command_buffer,
                    source_page->buffer,
                    op.buffer->buffer,
                    1,
                    &(VkBufferCopy){
                        .srcOffset = op.source_offset,
                        .dstOffset = 0,
                        .size = op.size,
                    });

            // The über optimization here would be to issue separate barriers per stages and accesses,
            // but I think it would drive up complexity for questionable (performance) benefit.
            if (op.buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
                buffer_dst_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                buffer_dst_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            } else if (op.buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
                buffer_dst_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                buffer_dst_access |= VK_ACCESS_INDEX_READ_BIT;
            } else {
                buffer_dst_stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                buffer_dst_access |= VK_ACCESS_SHADER_READ_BIT;
            }
        } else {
            nc_renderer_texture_t* texture = op.texture.texture;
            // The upload operation owns one contiguous chain. Vulkan needs one region per mip, but copies every
            // region with this single command instead of queueing and staging each level independently.
            VkBufferImageCopy regions[NC__RENDERER_MAX_TEXTURE_MIP_LEVELS];
            VkDeviceSize mip_offset = op.source_offset;
            uint32_t mip_width = (uint32_t)texture->width;
            uint32_t mip_height = (uint32_t)texture->height;
            for (uint8_t mip_level = 0; mip_level < texture->mip_level_count; mip_level++) {
                regions[mip_level] = (VkBufferImageCopy){
                    .bufferOffset = mip_offset,
                    .imageSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = mip_level,
                        .baseArrayLayer = op.texture.layer,
                        .layerCount = 1,
                    },
                    .imageExtent = { mip_width, mip_height, 1 },
                };

                const size_t mip_size = texture->mip_level_count > 1
                        ? ((size_t)mip_width + 3) / 4 * (((size_t)mip_height + 3) / 4) * 16
                        : op.size;
                mip_offset += mip_size;
                mip_width = mip_width > 1 ? mip_width >> 1 : 1;
                mip_height = mip_height > 1 ? mip_height >> 1 : 1;
            }
            NC_ASSERT(mip_offset == (VkDeviceSize)op.source_offset + op.size);
            vkCmdCopyBufferToImage(
                    renderer->frame_command_buffer,
                    source_page->buffer,
                    texture->image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    texture->mip_level_count,
                    regions);
        }
    }

    for (uint32_t i = 0; i < texture_barrier_count; i++) {
        image_barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        image_barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        image_barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier_textures[i]->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // All uploads precede all drawing, so one global memory dependency is enough.
    // The access and stage masks still limit the dependency's scope.
    const VkMemoryBarrier buffer_barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = buffer_dst_access,
    };
    if (buffer_dst_stages || texture_barrier_count > 0) {
        vkCmdPipelineBarrier(
                renderer->frame_command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                buffer_dst_stages | (texture_barrier_count > 0 ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0),
                0,
                buffer_dst_stages ? 1 : 0,
                buffer_dst_stages ? &buffer_barrier : NULL,
                0,
                NULL,
                texture_barrier_count,
                image_barriers);
    }

    nc__renderer_upload_op_vec_clear(&renderer->upload_ops);
    renderer->uploads_dirty = false;
    free((void*)barrier_textures);
    free(image_barriers);
    return true;
}

static bool nc__renderer_queue_buffer_upload_internal(
    nc_renderer_t* renderer,
    nc_renderer_buffer_t* buffer,
    const void* data,
    const uint32_t size
) {
    NC_ASSERT(size);

    uint32_t page_index = 0;
    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, 4, &page_index, &offset)) {
        return false;
    }

    memcpy((uint8_t*)renderer->transfer_pages.array[page_index].mapping + offset, data, size);
    nc__renderer_upload_op_vec_append(&renderer->upload_ops, (nc__renderer_upload_op_t){
        .kind = NC__RENDERER_UPLOAD_BUFFER,
        .source_page = page_index,
        .source_offset = offset,
        .size = size,
        .buffer = buffer,
    });
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

    uint32_t page_index = 0;
    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, 16, &page_index, &offset)) {
        return false;
    }

    memcpy((uint8_t*)renderer->transfer_pages.array[page_index].mapping + offset, data, size);
    nc__renderer_upload_op_vec_append(&renderer->upload_ops, (nc__renderer_upload_op_t){
        .kind = NC__RENDERER_UPLOAD_TEXTURE,
        .source_page = page_index,
        .source_offset = offset,
        .size = size,
        .texture = {
            .texture = texture,
            .layer = layer,
        },
    });
    renderer->uploads_dirty = true;
    return true;
}

static nc_renderer_texture_t* nc__renderer_create_texture_object(
    const nc_renderer_t* renderer,
    const VkFormat format,
    const int16_t width,
    const int16_t height,
    const uint16_t layer_count,
    const uint8_t mip_level_count
) {
    NC_ASSERT(width > 0);
    NC_ASSERT(height > 0);
    NC_ASSERT(layer_count > 0);
    NC_ASSERT(mip_level_count > 0 && mip_level_count <= NC__RENDERER_MAX_TEXTURE_MIP_LEVELS);

    nc_renderer_texture_t* result = calloc(1, sizeof(*result));
    result->width = width;
    result->height = height;
    result->layer_count = layer_count;
    result->mip_level_count = mip_level_count;
    // TODO: Does this affect transaction elimination in ARM?
    result->layout = VK_IMAGE_LAYOUT_UNDEFINED;

    NC__CHECK_VK_RESULT(vmaCreateImage(
            renderer->allocator,
            &(VkImageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = format,
                .extent = { (uint32_t)width, (uint32_t)height, 1 },
                .mipLevels = mip_level_count,
                .arrayLayers = layer_count,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                // TODO: Does this affect transaction elimination in ARM?
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
            &(VmaAllocationCreateInfo){
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            },
            &result->image,
            &result->allocation,
            NULL));

    NC__CHECK_VK_RESULT(vkCreateImageView(
            renderer->device,
            &(VkImageViewCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = result->image,
                .viewType = layer_count > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                .format = format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = mip_level_count,
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

static void nc__renderer_cancel_uploads_for_texture(nc_renderer_t* renderer, const nc_renderer_texture_t* texture) {
    // Upload operations retain raw texture pointers until the next flush. Texture-array creation can fail after
    // queuing some layers, so destroying the partially created texture must remove those operations first.
    // This makes texture destruction O(pending uploads), which is preferable to maintaining per-texture queue state
    // for an uncommon operation and keeps cancellation correct for every texture-destruction path.
    uint32_t i = 0;
    while (i < nc__renderer_upload_op_vec_count(&renderer->upload_ops)) {
        const nc__renderer_upload_op_t op = nc__renderer_upload_op_vec_get(&renderer->upload_ops, i);
        if (op.kind == NC__RENDERER_UPLOAD_TEXTURE && op.texture.texture == texture) {
            nc__renderer_upload_op_vec_remove(&renderer->upload_ops, i);
        } else {
            i++;
        }
    }
    renderer->uploads_dirty = nc__renderer_upload_op_vec_count(&renderer->upload_ops) > 0;
}

static void nc__renderer_cancel_uploads_for_buffer(nc_renderer_t* renderer, const nc_renderer_buffer_t* buffer) {
    // Buffer upload operations retain raw buffer pointers too. Remove them before freeing the wrapper so an upload
    // queued but not yet flushed cannot dereference freed memory. Like texture cancellation, this makes destruction
    // O(pending uploads) without adding per-buffer queue state.
    uint32_t i = 0;
    while (i < nc__renderer_upload_op_vec_count(&renderer->upload_ops)) {
        const nc__renderer_upload_op_t op = nc__renderer_upload_op_vec_get(&renderer->upload_ops, i);
        if (op.kind == NC__RENDERER_UPLOAD_BUFFER && op.buffer == buffer) {
            nc__renderer_upload_op_vec_remove(&renderer->upload_ops, i);
        } else {
            i++;
        }
    }
    renderer->uploads_dirty = nc__renderer_upload_op_vec_count(&renderer->upload_ops) > 0;
}

static void nc__renderer_destroy_or_retire_buffer(
    nc_renderer_t* renderer,
    VkBuffer buffer,
    VmaAllocation allocation
) {
    if (renderer->frame_in_progress || renderer->frame_fence_pending) {
        nc__renderer_retired_buffer_vec_append(
                &renderer->retired_buffers,
                (nc__renderer_retired_buffer_t){
                    .buffer = buffer,
                    .allocation = allocation,
                });
    } else {
        vmaDestroyBuffer(renderer->allocator, buffer, allocation);
    }
}

static void nc__renderer_destroy_or_retire_texture(nc_renderer_t* renderer, nc_renderer_texture_t* texture) {
    if (!texture) {
        return;
    }

    nc__renderer_cancel_uploads_for_texture(renderer, texture);

    if (renderer->frame_in_progress || renderer->frame_fence_pending) {
        nc__renderer_retired_texture_vec_append(
                &renderer->retired_textures,
                (nc__renderer_retired_texture_t){
                    .image = texture->image,
                    .image_view = texture->image_view,
                    .allocation = texture->allocation,
                });
        free(texture);
        return;
    }

    vkDestroyImageView(renderer->device, texture->image_view, NULL);
    if (texture->image) {
        vmaDestroyImage(renderer->allocator, texture->image, texture->allocation);
    }
    free(texture);
}

static void nc__renderer_destroy_retired_resources(nc_renderer_t* renderer) {
    // This renderer has one in-flight submission, and callers reclaim retired resources only after its fence signals.
    for (uint32_t i = 0; i < nc__renderer_retired_buffer_vec_count(&renderer->retired_buffers); i++) {
        const nc__renderer_retired_buffer_t buffer = nc__renderer_retired_buffer_vec_get(
                &renderer->retired_buffers,
                i);
        vmaDestroyBuffer(renderer->allocator, buffer.buffer, buffer.allocation);
    }
    nc__renderer_retired_buffer_vec_clear(&renderer->retired_buffers);

    for (uint32_t i = 0; i < nc__renderer_retired_texture_vec_count(&renderer->retired_textures); i++) {
        const nc__renderer_retired_texture_t texture = nc__renderer_retired_texture_vec_get(
                &renderer->retired_textures,
                i);
        vkDestroyImageView(renderer->device, texture.image_view, NULL);
        vmaDestroyImage(renderer->allocator, texture.image, texture.allocation);
    }
    nc__renderer_retired_texture_vec_clear(&renderer->retired_textures);
}

static bool nc__renderer_draw_chunk(
        nc_renderer_t* renderer,
        const nc_renderer_chunk_draw_t* draw,
        const vkm_mat4* view_projection,
        const vkm_vec4* quad_expansion,
        const float sunlight_intensity
) {
    if (draw->quad_count == 0) {
        return true;
    }

    const nc__renderer_chunk_uniforms_t uniforms = {
        .view_projection = *view_projection,
        .position = draw->position,
        .sunlight_intensity = sunlight_intensity,
        .quad_expansion = *quad_expansion,
    };
    nc__renderer_chunk_push_constants_t push_constants = {
        .quad_buffer = draw->chunk_buffer->address,
        .face_data_buffer = draw->face_data_buffer->address,
    };
    if (!nc__renderer_write_buffer_reference_data(
            renderer,
            &uniforms,
            sizeof(uniforms),
            &push_constants.uniforms)) {
        return false;
    }

    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            NC__RENDERER_VERTEX_PUSH_CONSTANT_OFFSET,
            2 * sizeof(VkDeviceAddress),
            &push_constants);
    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(VkDeviceAddress),
            2 * sizeof(VkDeviceAddress),
            &push_constants.uniforms);
    vkCmdDraw(renderer->frame_command_buffer, 4, draw->quad_count, 0, 0);
    return true;
}

static bool nc__renderer_draw_sky(
    nc_renderer_t* renderer,
    const nc_renderer_sky_draw_t* draw,
    const vkm_mat4* inverse_view_projection,
    const vkm_vec3* camera_position
) {
    const nc__renderer_sky_uniforms_t uniforms = {
        .inverse_view_projection = *inverse_view_projection,
        .camera_position = { { camera_position->x, camera_position->y, camera_position->z, 1.0f } },
        .gradient_colors = {
            draw->gradient_colors[0],
            draw->gradient_colors[1],
            draw->gradient_colors[2],
            draw->gradient_colors[3],
        },
        .gradient_stops = draw->gradient_stops,
    };
    VkDeviceAddress uniforms_address;
    if (!nc__renderer_write_buffer_reference_data(
            renderer,
            &uniforms,
            sizeof(uniforms),
            &uniforms_address)) {
        return false;
    }

    vkCmdBindPipeline(renderer->frame_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->sky_pipeline);
    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            NC__RENDERER_VERTEX_PUSH_CONSTANT_OFFSET,
            sizeof(uniforms_address),
            &uniforms_address);
    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            NC__RENDERER_FRAGMENT_PUSH_CONSTANT_OFFSET,
            sizeof(uniforms_address),
            &uniforms_address);
    vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);
    return true;
}

static bool nc__renderer_draw_block_highlight(
    nc_renderer_t* renderer,
    const nc_renderer_block_highlight_draw_t* draw,
    const vkm_mat4* view_projection
) {
    if (!draw->shown) {
        return true;
    }

    const nc__renderer_block_highlight_vertex_uniforms_t vertex_uniforms = {
        .view_projection = *view_projection,
        .block_position_and_scale = { { draw->position.x, draw->position.y, draw->position.z, 1.02f } },
    };

    const vkm_ubvec4 color = nc_cvar_get_block_highlight_color();
    const nc__renderer_block_highlight_fragment_uniforms_t fragment_uniforms = {
        .color = {
            .r = nc__renderer_srgb_to_linear((float)color.r / 255.0f),
            .g = nc__renderer_srgb_to_linear((float)color.g / 255.0f),
            .b = nc__renderer_srgb_to_linear((float)color.b / 255.0f),
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

    nc__renderer_address_push_constants_t push_constants;
    if (!nc__renderer_write_buffer_reference_data(
            renderer,
            &vertex_uniforms,
            sizeof(vertex_uniforms),
            &push_constants.data)) {
        return false;
    }

    vkCmdBindPipeline(renderer->frame_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            NC__RENDERER_VERTEX_PUSH_CONSTANT_OFFSET,
            sizeof(push_constants),
            &push_constants);

    if (!nc__renderer_write_buffer_reference_data(
            renderer,
            &fragment_uniforms,
            sizeof(fragment_uniforms),
            &push_constants.data)) {
        return false;
    }

    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            NC__RENDERER_FRAGMENT_PUSH_CONSTANT_OFFSET,
            sizeof(push_constants),
            &push_constants);
    vkCmdDraw(renderer->frame_command_buffer, 36, 1, 0, 0);
    return true;
}

// Vulkan scissors are integer rectangles, so conservatively cover the floating-point GUI clip.
static SDL_Rect nc__renderer_gui_scissor(const SDL_FRect gui_rect, const vkm_usvec2 framebuffer_size) {
    const int left = vkm_max((int)floorf(gui_rect.x), 0);
    const int top = vkm_max((int)floorf(gui_rect.y), 0);
    const int right = vkm_min((int)ceilf(gui_rect.x + gui_rect.w), framebuffer_size.x);
    const int bottom = vkm_min((int)ceilf(gui_rect.y + gui_rect.h), framebuffer_size.y);
    return (SDL_Rect){
        .x = left,
        .y = top,
        .w = vkm_max(right - left, 0),
        .h = vkm_max(bottom - top, 0),
    };
}

// Uploads the instance array once, then preserves Clay order while batching adjacent rectangles.
static bool nc__renderer_draw_overlay(nc_renderer_t* renderer, const nc_renderer_overlay_draw_t* draw) {
    if (draw->draw_command_count == 0) {
        return true;
    }

    vkm_mat4 pre_rotation;
    nc__renderer_get_pre_rotation_matrix(renderer, &pre_rotation);

    const vkm_usvec2 framebuffer_size = nc_renderer_get_framebuffer_size(renderer);
    const nc__renderer_gui_uniforms_t uniforms = {
        .transform = pre_rotation,
        .gui_to_ndc_scale = {
            {
                2.0f / (float)framebuffer_size.x,
                2.0f / (float)framebuffer_size.y,
            },
        },
    };
    nc__renderer_gui_push_constants_t push_constants;
    if (!nc__renderer_write_buffer_reference_data(
            renderer,
            &uniforms,
            sizeof(uniforms),
            &push_constants.uniforms)) {
        return false;
    }

    VkDeviceAddress rectangle_address = 0;
    if (draw->rectangle_count && !nc__renderer_write_buffer_reference_data(
            renderer,
            draw->rectangles,
            draw->rectangle_count * sizeof(draw->rectangles[0]),
            &rectangle_address)) {
        return false;
    }

    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    const nc_renderer_texture_t* bound_texture = NULL;
    for (uint32_t i = 0; i < draw->draw_command_count; i++) {
        const nc_renderer_overlay_draw_command_t* draw_command = &draw->draw_commands[i];
        SDL_FRect gui_scissor = draw_command->clip_rect;
        const nc_renderer_texture_t* texture;
        uint32_t instance_count;

        if (draw_command->type == NC_RENDERER_OVERLAY_COMMAND_RECTANGLES) {
            texture = draw->font_texture;
            instance_count = draw_command->rectangles.rectangle_count;
            push_constants.rectangles = rectangle_address +
                    draw_command->rectangles.first_rectangle * sizeof(draw->rectangles[0]);
            if (bound_pipeline != renderer->gui_pipeline) {
                vkCmdBindPipeline(
                        renderer->frame_command_buffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        renderer->gui_pipeline);
                bound_pipeline = renderer->gui_pipeline;
            }
        } else if (draw_command->type == NC_RENDERER_OVERLAY_COMMAND_IMAGE) {
            texture = draw_command->image.texture;
            instance_count = 1;
            if (!texture || texture->layer_count > 1) {
                continue;
            }

            const SDL_FRect bounds = draw_command->image.rectangle;
            nc_renderer_overlay_rectangle_t image = {
                .rectangle = bounds,
                .color = draw_command->image.color,
                .corner_radii = draw_command->image.corner_radii,
                .overlay_color = draw_command->image.overlay_color,
            };
            if (!nc__renderer_write_buffer_reference_data(
                    renderer,
                    &image,
                    sizeof(image),
                    &push_constants.rectangles)) {
                return false;
            }
            gui_scissor = bounds;
            if (draw_command->clip_enabled) {
                const float left = vkm_max(bounds.x, draw_command->clip_rect.x);
                const float top = vkm_max(bounds.y, draw_command->clip_rect.y);
                const float right = vkm_min(bounds.x + bounds.w, draw_command->clip_rect.x +
                        draw_command->clip_rect.w);
                const float bottom = vkm_min(bounds.y + bounds.h, draw_command->clip_rect.y +
                        draw_command->clip_rect.h);
                gui_scissor = (SDL_FRect){
                    left,
                    top,
                    vkm_max(right - left, 0.0f),
                    vkm_max(bottom - top, 0.0f),
                };
            }
            if (bound_pipeline != renderer->gui_image_pipeline) {
                vkCmdBindPipeline(
                        renderer->frame_command_buffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        renderer->gui_image_pipeline);
                bound_pipeline = renderer->gui_image_pipeline;
            }
        } else {
            continue;
        }

        if (!texture || texture->layer_count > 1) {
            continue;
        }
        if (texture != bound_texture &&
                !nc__renderer_bind_texture_descriptor_set(renderer, texture, renderer->gui_sampler)) {
            return false;
        }
        bound_texture = texture;

        SDL_Rect framebuffer_scissor;
        const SDL_Rect* framebuffer_scissor_pointer = NULL;
        if (draw_command->clip_enabled || draw_command->type == NC_RENDERER_OVERLAY_COMMAND_IMAGE) {
            framebuffer_scissor = nc__renderer_gui_scissor(gui_scissor, framebuffer_size);
            if (framebuffer_scissor.w <= 0 || framebuffer_scissor.h <= 0) {
                continue;
            }
            framebuffer_scissor_pointer = &framebuffer_scissor;
        }
        nc__renderer_set_viewport_and_scissor(renderer, framebuffer_scissor_pointer);
        vkCmdPushConstants(
                renderer->frame_command_buffer,
                renderer->pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT,
                NC__RENDERER_VERTEX_PUSH_CONSTANT_OFFSET,
                sizeof(push_constants),
                &push_constants);
        vkCmdDraw(renderer->frame_command_buffer, 4, instance_count, 0, 0);
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

        const vkm_vec2 ring_position = nc__renderer_pre_rotate_point(
                renderer,
                draw->analog_stick_ring_positions[i]);
        const vkm_vec2 stick_position = nc__renderer_pre_rotate_point(renderer, draw->analog_stick_positions[i]);
        uniforms.rings[i][0] = ring_position.x;
        uniforms.rings[i][1] = ring_position.y;
        uniforms.rings[i][2] = draw->analog_stick_ring_radius;
        uniforms.rings[i][3] = draw->analog_stick_ring_thickness;

        uniforms.sticks[i][0] = stick_position.x;
        uniforms.sticks[i][1] = stick_position.y;
        uniforms.sticks[i][2] = draw->analog_stick_radius;
    }

    uniforms.crosshair[0] = ((float)renderer->swapchain_extent.x - draw->crosshair_size) * 0.5f;
    uniforms.crosshair[1] = ((float)renderer->swapchain_extent.y - draw->crosshair_size) * 0.5f;
    uniforms.crosshair[2] = draw->crosshair_size;
    uniforms.crosshair[3] = draw->crosshair_size;

    nc__renderer_address_push_constants_t push_constants;
    if (!nc__renderer_write_buffer_reference_data(renderer, &uniforms, sizeof(uniforms), &push_constants.data) ||
            !nc__renderer_bind_texture_descriptor_set(
            renderer,
            renderer->procedural_overlay_crosshair_texture,
            renderer->gui_sampler)) {
        return false;
    }

    vkCmdPushConstants(
            renderer->frame_command_buffer,
            renderer->pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            NC__RENDERER_FRAGMENT_PUSH_CONSTANT_OFFSET,
            sizeof(push_constants),
            &push_constants);

    const vkm_usvec2 framebuffer_size = nc_renderer_get_framebuffer_size(renderer);
    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->procedural_overlay_invert_pipeline);

    // Draw each active ring inside its bounding-box scissor.
    for (uint32_t i = 0; i < 2; i++) {
        if (!draw->analog_sticks_active[i] ||
                draw->analog_stick_ring_radius <= 0.0f || draw->analog_stick_ring_thickness <= 0.0f) {
            continue;
        }

        const float outer_radius = draw->analog_stick_ring_radius + draw->analog_stick_ring_thickness * 0.5f;
        const vkm_vec2 position = draw->analog_stick_ring_positions[i];
        const SDL_Rect scissor = nc__renderer_gui_scissor(
                (SDL_FRect){
                    .x = position.x - outer_radius,
                    .y = position.y - outer_radius,
                    .w = outer_radius * 2.0f,
                    .h = outer_radius * 2.0f,
                },
                framebuffer_size);
        if (scissor.w <= 0 || scissor.h <= 0) {
            continue;
        }

        nc__renderer_set_viewport_and_scissor(renderer, &scissor);
        vkCmdPushConstants(
                renderer->frame_command_buffer,
                renderer->pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                NC__RENDERER_FRAGMENT_ELEMENT_PUSH_CONSTANT_OFFSET,
                sizeof(i),
                &i);
        vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);
    }

    // Ditto for the crosshair.
    const SDL_FRect crosshair_bounds = {
        .x = ((float)framebuffer_size.x - draw->crosshair_size) * 0.5f,
        .y = ((float)framebuffer_size.y - draw->crosshair_size) * 0.5f,
        .w = draw->crosshair_size,
        .h = draw->crosshair_size,
    };
    const SDL_Rect crosshair_scissor = nc__renderer_gui_scissor(crosshair_bounds, framebuffer_size);
    if (crosshair_scissor.w > 0 && crosshair_scissor.h > 0) {
        const uint32_t crosshair_element = 2;
        nc__renderer_set_viewport_and_scissor(renderer, &crosshair_scissor);
        vkCmdPushConstants(
                renderer->frame_command_buffer,
                renderer->pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                NC__RENDERER_FRAGMENT_ELEMENT_PUSH_CONSTANT_OFFSET,
                sizeof(crosshair_element),
                &crosshair_element);
        vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);
    }

    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->procedural_overlay_stick_pipeline);
    for (uint32_t i = 0; i < 2; i++) {
        if (!draw->analog_sticks_active[i] || draw->analog_stick_radius <= 0.0f) {
            continue;
        }

        const vkm_vec2 position = draw->analog_stick_positions[i];
        const SDL_Rect scissor = nc__renderer_gui_scissor(
                (SDL_FRect){
                    .x = position.x - draw->analog_stick_radius,
                    .y = position.y - draw->analog_stick_radius,
                    .w = draw->analog_stick_radius * 2.0f,
                    .h = draw->analog_stick_radius * 2.0f,
                },
                framebuffer_size);
        if (scissor.w <= 0 || scissor.h <= 0) {
            continue;
        }

        nc__renderer_set_viewport_and_scissor(renderer, &scissor);
        vkCmdPushConstants(
                renderer->frame_command_buffer,
                renderer->pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                NC__RENDERER_FRAGMENT_ELEMENT_PUSH_CONSTANT_OFFSET,
                sizeof(i),
                &i);
        vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);
    }
    return true;
}

nc_renderer_t* nc_renderer_init(const nc_renderer_create_info_t* info, nc_asset_manager_t* asset_manager) {
    nc_texture_baked_asset_t crosshair_texture_asset = { 0 };

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

    // Keep touch and mouse modalities distinct; player input should never see SDL's emulated mouse copy.
    sdl_result = SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0") && SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    NC_CHECK_SDL_RESULT(sdl_result);

    const char* enabled_device_extensions[NC__RENDERER_REQUIRED_EXTENSION_COUNT];
    if (!nc__renderer_create_instance(result)
            || !nc__renderer_create_window(result, info)
            || !nc__renderer_create_surface(result)
            || !nc__renderer_select_physical_device(result, info->prefer_low_power, enabled_device_extensions)
            || !nc__renderer_create_device(result, enabled_device_extensions)
            || !nc__renderer_create_descriptor_set_layouts(result)
            || !nc__renderer_create_descriptor_pool(result)
            || !nc__renderer_create_frame_resources(result)
            || !nc__renderer_initialize_transfer_pages(result)
            || !nc__renderer_create_sampler(result, VK_SAMPLER_MIPMAP_MODE_LINEAR, &result->chunk_sampler)
            || !nc__renderer_create_sampler(result, VK_SAMPLER_MIPMAP_MODE_NEAREST, &result->gui_sampler)
            || !nc__renderer_create_swapchain(result, VK_NULL_HANDLE)
            || !nc__renderer_create_pipelines(result, asset_manager)) {
        goto error;
    }

    if (!nc_asset_manager_get_texture_baked_asset(
            asset_manager,
            "novacube",
            "crosshair",
            NC_TEXTURE_TYPE_GUI,
            &crosshair_texture_asset)) {
        goto error;
    }
    result->procedural_overlay_crosshair_texture = nc_renderer_create_texture_from_baked_assets(
            result,
            &crosshair_texture_asset,
            1,
            NC_TEXTURE_TYPE_GUI,
            true);
    NC_CHECK_RESULT(result->procedural_overlay_crosshair_texture, "Failed to load the procedural crosshair texture.");

    nc_asset_manager_texture_baked_asset_fini(&crosshair_texture_asset);

    SDL_Log("Vulkan renderer initialized on %s.", result->physical_device_properties.deviceName);
    return result;

error:
    nc_asset_manager_texture_baked_asset_fini(&crosshair_texture_asset);
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
                // Android first creates its SurfaceView between visible system bars, then
                // resizes it after SDL's asynchronous immersive-mode request hides them.
                // Some Android versions report that transition only as a logical resize.
                renderer->swapchain_dirty = true;
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event->window.data1 > 0 && event->window.data2 > 0) {
                // SDL events are invalidation signals, not Vulkan state. Swapchain recreation
                // queries VkSurfaceCapabilitiesKHR again and installs its chosen extent.
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

    nc__renderer_destroy_retired_resources(renderer);
    if (renderer->retired_swapchains_pending_destruction) {
        // The completed submission waited on the acquisition of an image previously presented by the replacement
        // swapchain. Presentation of that image, and therefore every older swapchain, has finished.
        nc__renderer_destroy_retired_swapchains(renderer);
        renderer->retired_swapchains_pending_destruction = false;
    }

    if (!renderer->uploads_dirty && nc__renderer_upload_op_vec_count(&renderer->upload_ops) == 0) {
        nc__renderer_reset_and_trim_transfer_pages(renderer);
    }

    NC__CHECK_VK_RESULT(vkResetDescriptorPool(renderer->device, renderer->frame_descriptor_pool, 0));
    renderer->frame_descriptor_set_count = 0;

    if ((renderer->swapchain_dirty || renderer->surface_dirty) && !nc__renderer_recreate_swapchain(renderer)) {
        goto error;
    }

    NC__CHECK_VK_RESULT(vkBeginCommandBuffer(
            renderer->frame_command_buffer,
            &(VkCommandBufferBeginInfo){
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            }));
    renderer->frame_in_progress = true;

    renderer->frame_has_swapchain_image = false;
    renderer->frame_completes_present_retirement = false;
    renderer->frame_swapchain_image_index = 0;
    if (!renderer->foreground ||
            renderer->swapchain == VK_NULL_HANDLE ||
            renderer->swapchain_extent.x == 0 ||
            renderer->swapchain_extent.y == 0) {
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
    renderer->frame_completes_present_retirement =
            renderer->swapchain_retirement_probe_presented
            && renderer->frame_swapchain_image_index == renderer->swapchain_retirement_probe_image_index
            && nc__renderer_retired_swapchain_vec_count(&renderer->retired_swapchains) > 0;
    return true;

error:
    return false;
}

bool nc_renderer_end_frame(nc_renderer_t* renderer) {
    NC_ASSERT(renderer->frame_command_buffer);

    for (uint32_t i = 0; i < nc__renderer_transfer_page_vec_count(&renderer->transfer_pages); i++) {
        const nc__renderer_transfer_page_t* page = &renderer->transfer_pages.array[i];
        if (page->size > 0) {
            NC__CHECK_VK_RESULT(vmaFlushAllocation(renderer->allocator, page->allocation, 0, page->size));
        }
    }

    NC__CHECK_VK_RESULT(vkEndCommandBuffer(renderer->frame_command_buffer));
    NC__CHECK_VK_RESULT(vkResetFences(renderer->device, 1, &renderer->frame_fence));

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore* render_finished_semaphore = renderer->frame_has_swapchain_image
            ? &renderer->render_finished_semaphores[renderer->frame_swapchain_image_index]
            : NULL;
    NC__CHECK_VK_RESULT(vkQueueSubmit(
            renderer->queue,
            1,
            &(VkSubmitInfo){
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = renderer->frame_has_swapchain_image ? 1 : 0,
                .pWaitSemaphores = renderer->frame_has_swapchain_image ? &renderer->image_available_semaphore : NULL,
                .pWaitDstStageMask = renderer->frame_has_swapchain_image ? &wait_stage : NULL,
                .commandBufferCount = 1,
                .pCommandBuffers = &renderer->frame_command_buffer,
                .signalSemaphoreCount = renderer->frame_has_swapchain_image ? 1 : 0,
                .pSignalSemaphores = render_finished_semaphore,
            },
            renderer->frame_fence));
    renderer->frame_fence_pending = true;
    if (renderer->frame_completes_present_retirement) {
        // Destruction waits until the next begin-frame fence wait, which proves the acquire semaphore wait completed.
        renderer->retired_swapchains_pending_destruction = true;
    }

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
        if (!renderer->swapchain_retirement_probe_presented
                && nc__renderer_retired_swapchain_vec_count(&renderer->retired_swapchains) > 0
                && (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR)) {
            renderer->swapchain_retirement_probe_presented = true;
            renderer->swapchain_retirement_probe_image_index = renderer->frame_swapchain_image_index;
        }
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
    renderer->frame_completes_present_retirement = false;
    return true;

error:
    renderer->frame_in_progress = false;
    renderer->frame_completes_present_retirement = false;
    return false;
}

bool nc_renderer_set_relative_mouse_mode(nc_renderer_t* renderer, const bool enabled) {
    const bool sdl_result = SDL_SetWindowRelativeMouseMode(renderer->window, enabled);
    NC_CHECK_SDL_RESULT(sdl_result);
    return true;

error:
    return false;
}

bool nc_renderer_is_relative_mouse_mode(const nc_renderer_t* renderer) {
    return SDL_GetWindowRelativeMouseMode(renderer->window);
}

bool nc_renderer_is_foreground(const nc_renderer_t* renderer) {
    return renderer->foreground;
}

// The swapchain extent follows the surface's native orientation. GUI and input coordinates use the
// display orientation, so quarter-turn surface transforms swap its axes here.
vkm_usvec2 nc_renderer_get_framebuffer_size(const nc_renderer_t* renderer) {
    if (nc__renderer_surface_transform_swaps_extent(renderer->surface_transform)) {
        return (vkm_usvec2){ { renderer->swapchain_extent.y, renderer->swapchain_extent.x } };
    }

    return renderer->swapchain_extent;
}

vkm_usvec2 nc_renderer_get_window_size(const nc_renderer_t* renderer) {
    return renderer->window_size;
}

// Renderer and initial_size are optional, but specifying one requires the other.
// In case no initial size is given, this buffer is lazily-initialized.
nc_renderer_buffer_t* nc_renderer_create_buffer(
    nc_renderer_t* renderer,
    const nc_renderer_buffer_usage_t usage,
    const uint32_t initial_size
) {
    static const VkBufferUsageFlags nc_to_vk_usage[] = {
        [NC_RENDERER_BUFFER_USAGE_VERTEX] = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        [NC_RENDERER_BUFFER_USAGE_INDEX] = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        [NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ] =
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };

    NC_ASSERT(usage > 0 && usage <= NC_RENDERER_BUFFER_USAGE_COUNT);

    nc_renderer_buffer_t* result = calloc(1, sizeof(*result));
    result->usage = nc_to_vk_usage[usage] | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    result->capacity = initial_size;

    if (renderer) {
        NC_ASSERT(initial_size);
        NC__CHECK_VK_RESULT(vmaCreateBuffer(
                renderer->allocator,
                &(VkBufferCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = initial_size,
                    .usage = result->usage,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                },
                &(VmaAllocationCreateInfo){
                    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                },
                &result->buffer,
                &result->allocation,
                NULL));

        if (usage == NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ) {
            result->address = nc__renderer_get_buffer_address(renderer, result->buffer);
        }
    }

    return result;

error:
    free(result);
    return NULL;
}

void nc_renderer_destroy_buffer(nc_renderer_t* renderer, nc_renderer_buffer_t* buffer) {
    if (!buffer) {
        return;
    }

    nc__renderer_cancel_uploads_for_buffer(renderer, buffer);
    nc__renderer_destroy_or_retire_buffer(renderer, buffer->buffer, buffer->allocation);
    free(buffer);
}

bool nc_renderer_queue_buffer_upload(
    nc_renderer_t* renderer,
    nc_renderer_buffer_t* buffer,
    const void* data,
    const uint32_t size
) {
    if (size > buffer->capacity) {
        const uint32_t new_capacity = nc__renderer_next_capacity(buffer->capacity, size, size);
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
                    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                },
                &new_buffer,
                &new_allocation,
                NULL));

        nc__renderer_destroy_or_retire_buffer(renderer, buffer->buffer, buffer->allocation);
        buffer->buffer = new_buffer;
        buffer->allocation = new_allocation;
        buffer->capacity = new_capacity;
        buffer->address = nc__renderer_get_buffer_address(renderer, buffer->buffer);
    }

    return nc__renderer_queue_buffer_upload_internal(renderer, buffer, data, size);

error:
    return false;
}

nc_renderer_texture_t* nc_renderer_create_rgba_texture_2d(
    nc_renderer_t* renderer,
    const bool is_color_data,
    const int16_t width,
    const int16_t height,
    const void* pixels
) {
    nc_renderer_texture_t* result = nc__renderer_create_texture_object(
            renderer,
            is_color_data ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
            width,
            height,
            1,
            1);
    if (!result) {
        return NULL;
    }

    const uint32_t size = (uint32_t)width * (uint32_t)height * 4;
    if (!nc__renderer_queue_texture_upload(renderer, result, 0, pixels, size)) {
        nc__renderer_destroy_or_retire_texture(renderer, result);
        return NULL;
    }

    return result;
}

nc_renderer_texture_t* nc_renderer_create_texture_from_baked_assets(
    nc_renderer_t* renderer,
    const nc_texture_baked_asset_t* assets,
    const uint16_t asset_count,
    const nc_texture_type_t texture_type,
    const bool is_color_data
) {
    NC_ASSERT(asset_count >= 1 && asset_count <= 2048);
    NC_ASSERT(texture_type == NC_TEXTURE_TYPE_BLOCK || texture_type == NC_TEXTURE_TYPE_GUI);
    NC_ASSERT(assets[0].width > 0 && assets[0].height > 0);

    const uint8_t mip_level_count = texture_type == NC_TEXTURE_TYPE_BLOCK
            ? nc__renderer_mip_level_count((uint32_t)assets[0].width, (uint32_t)assets[0].height)
            : 1;
    nc_renderer_texture_t* result = nc__renderer_create_texture_object(
            renderer,
#ifdef ANDROID
            is_color_data ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
#else
            is_color_data ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK,
#endif
            assets[0].width,
            assets[0].height,
            asset_count,
            mip_level_count);
    if (!result) {
        return NULL;
    }

    for (uint16_t i = 0; i < asset_count; i++) {
        const nc_texture_baked_asset_t* asset = &assets[i];

        if (asset->width <= 0 || asset->height <= 0) {
            NC_SET_ERROR("Texture #%d has invalid dimensions %dx%d.", i, asset->width, asset->height);
            nc__renderer_destroy_or_retire_texture(renderer, result);
            return NULL;
        }
        if (asset->width != assets[0].width || asset->height != assets[0].height) {
            NC_SET_ERROR(
                    "Texture #%d is %dx%d while the first texture is %dx%d.",
                    i,
                    asset->width,
                    asset->height,
                    assets[0].width,
                    assets[0].height);
            nc__renderer_destroy_or_retire_texture(renderer, result);
            return NULL;
        }

        const size_t upload_size = nc__renderer_block_compressed_mip_chain_size(
                (uint32_t)asset->width,
                (uint32_t)asset->height,
                mip_level_count);
        NC_ASSERT(upload_size <= UINT32_MAX);
        const bool upload_result = nc__renderer_queue_texture_upload(
                renderer,
                result,
                i,
                asset->pixels,
                (uint32_t)upload_size);
        if (!upload_result) {
            nc__renderer_destroy_or_retire_texture(renderer, result);
            return NULL;
        }
    }

    return result;
}

void nc_renderer_destroy_texture(nc_renderer_t* renderer, nc_renderer_texture_t* texture) {
    nc__renderer_destroy_or_retire_texture(renderer, texture);
}

bool nc_renderer_draw(nc_renderer_t* renderer, const nc_renderer_frame_t* frame) {
    NC_ASSERT(renderer->frame_command_buffer);
    NC_ASSERT(frame->view_projection);
    NC_ASSERT(frame->sky_draw);
    NC_ASSERT(frame->sunlight_intensity >= 0.0f && frame->sunlight_intensity <= 1.0f);

    if (!nc__renderer_flush_uploads(renderer)) {
        return false;
    }

    if (!renderer->frame_has_swapchain_image) {
        return true;
    }

    vkm_mat4 view_projection;
    vkm_mat4 inverse_view_projection;
    nc__renderer_pre_rotate_view_projection(renderer, frame->view_projection, &view_projection);
    vkm_invert(&view_projection, &inverse_view_projection);

    nc__renderer_initialize_render_pass_attachments(renderer);
    const VkClearValue clear_values[] = {
        // swapchain image (unused, the load operation is "don't care")
        { .color = { .float32 = { 0.0f } } },
        // depth
        { .depthStencil = { .depth = 1.0f } },
        // accumulation
        { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } } },
        // reveal
        { .color = { .float32 = { 1.0f } } },
    };
    vkCmdBeginRenderPass(
            renderer->frame_command_buffer,
            &(VkRenderPassBeginInfo){
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = renderer->render_pass,
                .framebuffer = renderer->framebuffers[renderer->frame_swapchain_image_index],
                .renderArea = {
                    .extent = { renderer->swapchain_extent.x, renderer->swapchain_extent.y },
                },
                .clearValueCount = NC_COUNTOF(clear_values),
                .pClearValues = clear_values,
            },
            VK_SUBPASS_CONTENTS_INLINE);

    nc__renderer_set_viewport_and_scissor(renderer, NULL);

    const vkm_vec4 quad_expansion = { {
        2.0f * NC__RENDERER_QUAD_EXPANSION_PIXELS / (float)renderer->swapchain_extent.x,
        2.0f * NC__RENDERER_QUAD_EXPANSION_PIXELS / (float)renderer->swapchain_extent.y,
        (float)renderer->swapchain_extent.x / (2.0f * NC__RENDERER_QUAD_EXPANSION_PIXELS),
        (float)renderer->swapchain_extent.y / (2.0f * NC__RENDERER_QUAD_EXPANSION_PIXELS),
    } };

    if (!nc__renderer_bind_texture_descriptor_set(renderer, frame->terrain_texture_array, renderer->chunk_sampler)) {
        return false;
    }

#pragma region Opaque pass
    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->opaque_chunk_pipeline);
    for (uint32_t i = 0; i < nc_renderer_chunk_draw_vec_count(frame->opaque_chunk_draws); i++) {
        const nc_renderer_chunk_draw_t draw = nc_renderer_chunk_draw_vec_get(frame->opaque_chunk_draws, i);
        if (!nc__renderer_draw_chunk(
                renderer,
                &draw,
                &view_projection,
                &quad_expansion,
                frame->sunlight_intensity)) {
            return false;
        }
    }

    if (!nc__renderer_draw_sky(
            renderer,
            frame->sky_draw,
            &inverse_view_projection,
            &frame->camera_position)) {
        return false;
    }
#pragma endregion

#pragma region Transparent pass
    vkCmdNextSubpass(renderer->frame_command_buffer, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->transparent_chunk_pipeline);
    for (uint32_t i = 0; i < nc_renderer_chunk_draw_vec_count(frame->transparent_chunk_draws); i++) {
        const nc_renderer_chunk_draw_t draw = nc_renderer_chunk_draw_vec_get(frame->transparent_chunk_draws, i);
        if (!nc__renderer_draw_chunk(
                renderer,
                &draw,
                &view_projection,
                &quad_expansion,
                frame->sunlight_intensity)) {
            return false;
        }
    }
#pragma endregion

#pragma region Composite pass
    vkCmdNextSubpass(renderer->frame_command_buffer, VK_SUBPASS_CONTENTS_INLINE);
    if (!nc__renderer_bind_composite_descriptor_set(renderer)) {
        return false;
    }
    vkCmdBindPipeline(
            renderer->frame_command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->composite_chunk_pipeline);
    vkCmdDraw(renderer->frame_command_buffer, 3, 1, 0, 0);
#pragma endregion

    if (!nc__renderer_draw_block_highlight(renderer, frame->block_highlight_draw, &view_projection)) {
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

void nc_renderer_get_window_safe_area(const nc_renderer_t* renderer, SDL_Rect* rect) {
    if (!SDL_GetWindowSafeArea(renderer->window, rect)) {
        SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Failed to get the window's safe area. Falling back to the entire window.");
        SDL_ClearError();
        const vkm_usvec2 window_size = nc_renderer_get_window_size(renderer);
        *rect = (SDL_Rect){ .x = 0, .y = 0, .w = window_size.x, .h = window_size.y };
    }
}

void nc_renderer_fini(nc_renderer_t* renderer) {
    if (!renderer) {
        return;
    }

    if (renderer->device) {
        nc__renderer_wait_idle(renderer);
        nc__renderer_destroy_retired_resources(renderer);

        nc__renderer_destroy_descriptor_state(renderer);
        nc__renderer_destroy_or_retire_texture(renderer, renderer->procedural_overlay_crosshair_texture);

        nc__renderer_destroy_pipelines(renderer);
        vkDestroySampler(renderer->device, renderer->chunk_sampler, NULL);
        vkDestroySampler(renderer->device, renderer->gui_sampler, NULL);
        nc__renderer_destroy_swapchain(renderer);
        vkDestroyRenderPass(renderer->device, renderer->render_pass, NULL);
        for (uint32_t i = 0; i < nc__renderer_transfer_page_vec_count(&renderer->transfer_pages); i++) {
            const nc__renderer_transfer_page_t page = renderer->transfer_pages.array[i];
            vmaDestroyBuffer(renderer->allocator, page.buffer, page.allocation);
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

    nc__renderer_transfer_page_vec_fini(&renderer->transfer_pages);
    nc__renderer_upload_op_vec_fini(&renderer->upload_ops);
    nc__renderer_retired_buffer_vec_fini(&renderer->retired_buffers);
    nc__renderer_retired_texture_vec_fini(&renderer->retired_textures);
    nc__renderer_retired_swapchain_vec_fini(&renderer->retired_swapchains);
    if (renderer->window) {
        SDL_DestroyWindow(renderer->window);
    }
    SDL_Vulkan_UnloadLibrary();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    free(renderer);
}
