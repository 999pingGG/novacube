#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <novacube/error_handling.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>

#ifdef ANDROID
#define NC__RENDERER_ASTC_TEXTURES 1
#else
#define NC__RENDERER_ASTC_TEXTURES 0
#endif

#define NC__RENDERER_INITIAL_TRANSFER_CAPACITY 65536
#define NC__RENDERER_CLEAR_RED 0.53f
#define NC__RENDERER_CLEAR_GREEN 0.81f
#define NC__RENDERER_CLEAR_BLUE 0.92f

typedef struct nc__renderer_texture_file_data_t {
    uint8_t* bytes;
    SDL_GPUTextureFormat format;
    uint32_t size;
    int16_t width;
    int16_t height;
} nc__renderer_texture_file_data_t;

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
    uint32_t source_offset;
    uint32_t size;
    nc__renderer_upload_kind_t kind;
    bool cycle;
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

typedef struct nc_renderer_texture_t {
    SDL_GPUTexture* gpu_texture;
    SDL_GPUTextureFormat format;
    int32_t width;
    int32_t height;
    uint16_t layer_count;
    bool is_array;
} nc_renderer_texture_t;

typedef struct nc_renderer_buffer_t {
    SDL_GPUBuffer* gpu_buffer;
    SDL_GPUBufferUsageFlags usage;
    uint32_t capacity;
    uint64_t queued_upload_frame;
} nc_renderer_buffer_t;

typedef struct nc_renderer_t {
    SDL_GPUDevice* gpu_device;
    SDL_Window* window;
    SDL_GPUTexture* depth_texture;
    SDL_GPUTextureFormat swapchain_format;
    vkm_usvec2 window_size;
    vkm_usvec2 viewport;
    bool foreground;

    SDL_GPUGraphicsPipeline* terrain_pipeline;
    SDL_GPUGraphicsPipeline* gui_pipeline;
    SDL_GPUSampler* terrain_sampler;
    SDL_GPUSampler* gui_sampler;

    SDL_GPUTransferBuffer* transfer_buffer;
    void* mapped_transfer_buffer;
    uint32_t transfer_size;
    uint32_t transfer_capacity;
    bool transfer_buffer_needs_cycle;

    nc__renderer_upload_op_t* upload_ops;
    uint32_t upload_count;
    uint32_t upload_capacity;
    bool uploads_dirty;

    SDL_GPUCommandBuffer* frame_command_buffer;
    SDL_GPUTexture* frame_swapchain_texture;
    uint64_t frame_id;
} nc_renderer_t;

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

static void nc__renderer_free_texture_file_data(nc__renderer_texture_file_data_t* data) {
    free(data->bytes);
    *data = (nc__renderer_texture_file_data_t){ 0 };
}

static SDL_GPUShader* nc__renderer_load_shader(
    nc_renderer_t* renderer,
    const char* path,
    const SDL_GPUShaderStage stage,
    const Uint32 sampler_count,
    const Uint32 uniform_buffer_count
) {
    void* code = NULL;
    size_t code_size = 0;
    SDL_GPUShader* shader = NULL;

    code = SDL_LoadFile(path, &code_size);
    NC_CHECK_SDL_RESULT(code);

    shader = SDL_CreateGPUShader(renderer->gpu_device, &(SDL_GPUShaderCreateInfo){
        .code_size = code_size,
        .code = code,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = stage,
        .num_samplers = sampler_count,
        .num_uniform_buffers = uniform_buffer_count,
    });
    NC_CHECK_SDL_RESULT(shader);

    free(code);
    return shader;

error:
    free(code);
    return NULL;
}

static void nc__renderer_destroy_texture_object(nc_renderer_t* renderer, nc_renderer_texture_t* texture) {
    if (!texture) {
        return;
    }

    SDL_ReleaseGPUTexture(renderer->gpu_device, texture->gpu_texture);
    free(texture);
}

static bool nc__renderer_create_depth_texture(nc_renderer_t* renderer) {
    SDL_ReleaseGPUTexture(renderer->gpu_device, renderer->depth_texture);

    renderer->depth_texture = SDL_CreateGPUTexture(renderer->gpu_device, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = renderer->viewport.x,
        .height = renderer->viewport.y,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    });
    NC_CHECK_SDL_RESULT(renderer->depth_texture);

    return true;

error:
    return false;
}

static void nc__renderer_reserve_upload_ops(nc_renderer_t* renderer, const uint32_t additional_ops) {
    NC_ASSERT(additional_ops);

    const uint32_t required = renderer->upload_count + additional_ops;
    NC_ASSERT(required > renderer->upload_count);

    if (required <= renderer->upload_capacity) {
        return;
    }

    const uint32_t new_capacity = nc__renderer_next_capacity(renderer->upload_capacity, required, 16);
    renderer->upload_ops = realloc(renderer->upload_ops, new_capacity * sizeof(*renderer->upload_ops));
    renderer->upload_capacity = new_capacity;
}

static bool nc__renderer_ensure_transfer_mapping(nc_renderer_t* renderer) {
    if (renderer->mapped_transfer_buffer) {
        return true;
    }

    renderer->mapped_transfer_buffer = SDL_MapGPUTransferBuffer(
            renderer->gpu_device,
            renderer->transfer_buffer,
            renderer->transfer_buffer_needs_cycle);
    NC_CHECK_SDL_RESULT(renderer->mapped_transfer_buffer);

    renderer->transfer_buffer_needs_cycle = false;
    return true;

error:
    return false;
}

static bool nc__renderer_reserve_transfer_bytes(nc_renderer_t* renderer, const uint32_t size, uint32_t* out_offset) {
    const uint32_t required = renderer->transfer_size + size;
    NC_ASSERT(required > renderer->transfer_size);

    if (required > renderer->transfer_capacity) {
        const uint32_t new_capacity = nc__renderer_next_capacity(
                renderer->transfer_capacity,
                required,
                NC__RENDERER_INITIAL_TRANSFER_CAPACITY);
        SDL_GPUTransferBuffer* new_transfer_buffer = SDL_CreateGPUTransferBuffer(
                renderer->gpu_device,
                &(SDL_GPUTransferBufferCreateInfo){
                    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                    .size = new_capacity,
                });
        NC_CHECK_SDL_RESULT(new_transfer_buffer);

        void* new_mapped_transfer_buffer = NULL;
        if (renderer->mapped_transfer_buffer) {
            new_mapped_transfer_buffer = SDL_MapGPUTransferBuffer(renderer->gpu_device, new_transfer_buffer, false);
            NC_CHECK_SDL_RESULT(new_mapped_transfer_buffer);

            memcpy(new_mapped_transfer_buffer, renderer->mapped_transfer_buffer, renderer->transfer_size);
            SDL_UnmapGPUTransferBuffer(renderer->gpu_device, renderer->transfer_buffer);
        }

        SDL_ReleaseGPUTransferBuffer(renderer->gpu_device, renderer->transfer_buffer);
        renderer->transfer_buffer = new_transfer_buffer;
        renderer->mapped_transfer_buffer = new_mapped_transfer_buffer;
        renderer->transfer_capacity = new_capacity;
        renderer->transfer_buffer_needs_cycle = false;
    }

    if (!nc__renderer_ensure_transfer_mapping(renderer)) {
        goto error;
    }

    *out_offset = renderer->transfer_size;
    renderer->transfer_size = required;
    return true;

error:
    return false;
}

static bool nc__renderer_queue_buffer_upload_internal(
    nc_renderer_t* renderer,
    nc_renderer_buffer_t* buffer,
    const void* data,
    const uint32_t size
) {
    NC_ASSERT(size);

    nc__renderer_reserve_upload_ops(renderer, 1);

    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, &offset)) {
        return false;
    }

    memcpy((uint8_t*)renderer->mapped_transfer_buffer + offset, data, size);
    renderer->upload_ops[renderer->upload_count++] = (nc__renderer_upload_op_t){
        .kind = NC__RENDERER_UPLOAD_BUFFER,
        .source_offset = offset,
        .size = size,
        .buffer = buffer,
        .cycle = buffer->queued_upload_frame != renderer->frame_id,
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

    nc__renderer_reserve_upload_ops(renderer, 1);

    uint32_t offset = 0;
    if (!nc__renderer_reserve_transfer_bytes(renderer, size, &offset)) {
        return false;
    }

    memcpy((char*)renderer->mapped_transfer_buffer + offset, data, size);
    renderer->upload_ops[renderer->upload_count++] = (nc__renderer_upload_op_t){
        .kind = NC__RENDERER_UPLOAD_TEXTURE,
        .source_offset = offset,
        .size = size,
        .texture = {
            .texture = texture,
            .layer = layer,
        },
        .cycle = false,
    };
    renderer->uploads_dirty = true;
    return true;
}

static bool nc__renderer_flush_uploads(nc_renderer_t* renderer) {
    if (!renderer->uploads_dirty || renderer->upload_count == 0) {
        return true;
    }

    NC_ASSERT(renderer->frame_command_buffer);

    if (renderer->mapped_transfer_buffer) {
        SDL_UnmapGPUTransferBuffer(renderer->gpu_device, renderer->transfer_buffer);
        renderer->mapped_transfer_buffer = NULL;
    }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(renderer->frame_command_buffer);
    NC_CHECK_SDL_RESULT(copy_pass);

    for (uint32_t i = 0; i < renderer->upload_count; i++) {
        const nc__renderer_upload_op_t* op = &renderer->upload_ops[i];
        if (op->kind == NC__RENDERER_UPLOAD_BUFFER) {
            SDL_UploadToGPUBuffer(
                    copy_pass,
                    &(SDL_GPUTransferBufferLocation){
                        .transfer_buffer = renderer->transfer_buffer,
                        .offset = op->source_offset,
                    },
                    &(SDL_GPUBufferRegion){
                        .buffer = op->buffer->gpu_buffer,
                        .offset = 0,
                        .size = op->size,
                    },
                    op->cycle);
        } else {
            SDL_UploadToGPUTexture(
                    copy_pass,
                    &(SDL_GPUTextureTransferInfo){
                        .transfer_buffer = renderer->transfer_buffer,
                        .offset = op->source_offset,
                    },
                    &(SDL_GPUTextureRegion){
                        .texture = op->texture.texture->gpu_texture,
                        .layer = op->texture.layer,
                        .w = op->texture.texture->width,
                        .h = op->texture.texture->height,
                        .d = 1,
                    },
                    op->cycle);
        }
    }
    SDL_EndGPUCopyPass(copy_pass);

    renderer->transfer_size = 0;
    renderer->upload_count = 0;
    renderer->uploads_dirty = false;
    renderer->transfer_buffer_needs_cycle = true;
    return true;

error:
    return false;
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
                "Unsupported ASTC block size (%ix%ix%i, must be 4x4x1): %s",
                header->block_x,
                header->block_y,
                header->block_z,
                path);
        goto error;
    }
    if (nc__renderer_read_u24(header->dim_z) != 1) {
        NC_SET_ERROR("The ASTC depth is %i, must be 1: %s", header->dim_z, path);
        goto error;
    }

    uint32_t x = nc__renderer_read_u24(header->dim_x);
    uint32_t y = nc__renderer_read_u24(header->dim_y);

    if (x > INT16_MAX || y > INT16_MAX) {
        NC_SET_ERROR(
                "Texture %s is %ix%i, the max dimensions are %ix%i.",
                x,
                y,
                INT16_MAX,
                INT16_MAX);
    }

    out_texture->size = (uint32_t)(file_size - sizeof(*header));
    out_texture->bytes = malloc(out_texture->size);
    memcpy(out_texture->bytes, file_bytes + sizeof(*header), out_texture->size);
    out_texture->width = (int16_t)x;
    out_texture->height = (int16_t)y;
    out_texture->format = SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;

    free(file_bytes);
    return true;

error:
    free(file_bytes);
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
        return false;
    }

    if (surface->w > INT16_MAX || surface->h > INT16_MAX) {
        NC_SET_ERROR(
                "Texture %s is %ix%i, the max dimensions are %ix%i.",
                surface->w,
                surface->h,
                INT16_MAX,
                INT16_MAX);
    }

    *out_texture = (nc__renderer_texture_file_data_t){
        .size = (uint32_t)surface->w * (uint32_t)surface->h * 4,
        .width = (int16_t)surface->w,
        .height = (int16_t)surface->h,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    };
    out_texture->bytes = memcpy(malloc(out_texture->size), surface->pixels, out_texture->size);

    SDL_DestroySurface(surface);
    return true;

error:
    return false;
}
#endif

static nc_renderer_texture_t* nc__renderer_create_texture_object(
    nc_renderer_t* renderer,
    const SDL_GPUTextureFormat format,
    const int16_t width,
    const int16_t height,
    const uint16_t layer_count,
    const bool is_array
) {
    NC_ASSERT(width > 0);
    NC_ASSERT(height > 0);

    SDL_GPUTexture* gpu_texture = SDL_CreateGPUTexture(renderer->gpu_device, &(SDL_GPUTextureCreateInfo){
        .type = is_array ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D,
        .format = format,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = width,
        .height = height,
        .layer_count_or_depth = layer_count,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    });
    NC_CHECK_SDL_RESULT(gpu_texture);

    nc_renderer_texture_t* result = malloc(sizeof(*result));
    *result = (nc_renderer_texture_t){
        .gpu_texture = gpu_texture,
        .format = format,
        .width = width,
        .height = height,
        .layer_count = layer_count,
        .is_array = is_array,
    };
    return result;

error:
    return NULL;
}

static void nc__renderer_draw_opaque(
    nc_renderer_t* renderer,
    SDL_GPURenderPass* render_pass,
    const nc_renderer_opaque_draw_t* draw
) {
    if (draw->instance_count == 0) {
        return;
    }

    NC_ASSERT(draw->texture->is_array);

    SDL_BindGPUGraphicsPipeline(render_pass, renderer->terrain_pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
        .buffer = draw->instance_buffer->gpu_buffer,
        .offset = 0,
    }, 1);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &(SDL_GPUTextureSamplerBinding){
        .texture = draw->texture->gpu_texture,
        .sampler = renderer->terrain_sampler,
    }, 1);
    SDL_PushGPUVertexUniformData(renderer->frame_command_buffer, 0, draw->view_projection, sizeof(*draw->view_projection));
    SDL_DrawGPUPrimitives(render_pass, 36, draw->instance_count, 0, 0);
}

static void nc__renderer_draw_overlay(
    nc_renderer_t* renderer,
    SDL_GPURenderPass* render_pass,
    const nc_renderer_overlay_draw_t* draw
) {
    if (draw->draw_command_count == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, renderer->gui_pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
        .buffer = draw->vertex_buffer->gpu_buffer,
        .offset = 0,
    }, 1);
    SDL_BindGPUIndexBuffer(render_pass, &(SDL_GPUBufferBinding){
        .buffer = draw->index_buffer->gpu_buffer,
        .offset = 0,
    }, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    const float uniforms[] = {
        2.0f / (float)renderer->viewport.x,
        -2.0f / (float)renderer->viewport.y,
        -1.0f,
        1.0f,
    };
    SDL_PushGPUVertexUniformData(renderer->frame_command_buffer, 0, uniforms, sizeof(uniforms));

    for (uint32_t i = 0; i < draw->draw_command_count; i++) {
        const nc_renderer_overlay_draw_command_t* draw_command = &draw->draw_commands[i];
        const nc_renderer_texture_t* texture = draw_command->texture;
        if (!texture || texture->is_array) {
            continue;
        }

        SDL_BindGPUFragmentSamplers(render_pass, 0, &(SDL_GPUTextureSamplerBinding){
            .texture = texture->gpu_texture,
            .sampler = renderer->gui_sampler,
        }, 1);
        SDL_SetGPUScissor(render_pass, &draw_command->clip_rect);
        SDL_DrawGPUIndexedPrimitives(
                render_pass,
                draw_command->element_count,
                1,
                draw_command->first_index,
                0,
                0);
    }
}

nc_renderer_t* nc_renderer_init(const nc_renderer_create_info_t* info) {
    SDL_PropertiesID props = 0;
    SDL_GPUShader* vertex_shader = NULL;
    SDL_GPUShader* fragment_shader = NULL;
    nc_renderer_t* result = calloc(1, sizeof(*result));

    result->foreground = true;

    bool sdl_result = SDL_InitSubSystem(SDL_INIT_VIDEO);
    NC_CHECK_SDL_RESULT(sdl_result);

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    props = SDL_CreateProperties();
    NC_CHECK_SDL_RESULT(props);

    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    sdl_result &= SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
#ifdef NDEBUG
            false);
#else
            true);
#endif
    sdl_result &= SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, true);
    sdl_result &= SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, true);
    sdl_result &= SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    sdl_result &= SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
    sdl_result &= SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
    sdl_result &= SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);

    SDL_GPUVulkanOptions options = {
        .vulkan_api_version = VK_API_VERSION_1_0,
    };
    sdl_result &= SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &options);
    NC_CHECK_SDL_RESULT(sdl_result);

    result->gpu_device = SDL_CreateGPUDeviceWithProperties(props);
    NC_CHECK_SDL_RESULT(result->gpu_device);

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

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
        case NC_VIDEO_MODE_FULLSCREEN:
            window_flags |= SDL_WINDOW_FULLSCREEN;
            break;
    }
#endif

    result->window = SDL_CreateWindow(info->window_title, info->window_width, info->window_height, window_flags);
    NC_CHECK_SDL_RESULT(result->window);

#ifndef ANDROID
    if (exclusive) {
        SDL_DisplayMode display_mode;
        sdl_result = SDL_GetClosestFullscreenDisplayMode(
                SDL_GetDisplayForWindow(result->window),
                info->window_width,
                info->window_height,
                (float)info->refresh_rate,
                true, &display_mode);
        if (!sdl_result) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetClosestFullscreenDisplayMode() failed: %s", SDL_GetError());
            SDL_ClearError();
        } else {
            sdl_result = SDL_SetWindowFullscreenMode(result->window, &display_mode);
            if (!sdl_result) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetWindowFullscreenMode() failed: %s", SDL_GetError());
                SDL_ClearError();
            }
        }
    }
#endif

    int width;
    int height;
    sdl_result = SDL_GetWindowSize(result->window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);

    result->window_size.x = (uint16_t)width;
    result->window_size.y = (uint16_t)height;

    sdl_result = SDL_GetWindowSizeInPixels(result->window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);

    result->viewport.x = (uint16_t)width;
    result->viewport.y = (uint16_t)height;

    sdl_result = SDL_ClaimWindowForGPUDevice(result->gpu_device, result->window);
    NC_CHECK_SDL_RESULT(sdl_result);

    result->swapchain_format = SDL_GetGPUSwapchainTextureFormat(result->gpu_device, result->window);
    if (!nc__renderer_create_depth_texture(result)) {
        goto error;
    }

    result->transfer_capacity = NC__RENDERER_INITIAL_TRANSFER_CAPACITY;
    result->transfer_buffer = SDL_CreateGPUTransferBuffer(result->gpu_device, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = result->transfer_capacity,
    });
    NC_CHECK_SDL_RESULT(result->transfer_buffer);

    result->terrain_sampler = SDL_CreateGPUSampler(result->gpu_device, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
    });
    NC_CHECK_SDL_RESULT(result->terrain_sampler);

    result->gui_sampler = SDL_CreateGPUSampler(result->gpu_device, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    });
    NC_CHECK_SDL_RESULT(result->gui_sampler);

    vertex_shader = nc__renderer_load_shader(
            result,
            NC__RENDERER_ASTC_TEXTURES ? "shaders/cube-vert.spv" : "assets/shaders/cube-vert.spv",
            SDL_GPU_SHADERSTAGE_VERTEX,
            0,
            1);
    fragment_shader = nc__renderer_load_shader(
            result,
            NC__RENDERER_ASTC_TEXTURES ? "shaders/cube-frag.spv" : "assets/shaders/cube-frag.spv",
            SDL_GPU_SHADERSTAGE_FRAGMENT,
            1,
            0);
    NC_CHECK_RESULT(vertex_shader && fragment_shader, "Failed to load the terrain shaders.");

    result->terrain_pipeline = SDL_CreateGPUGraphicsPipeline(result->gpu_device, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]){
                {
                    .slot = 0,
                    .pitch = 4,
                    .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
                },
            },
            .num_vertex_buffers = 1,
            .vertex_attributes = (SDL_GPUVertexAttribute[]){
                {
                    .location = 0,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4,
                    .offset = 0,
                },
            },
            .num_vertex_attributes = 1,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_clip = true,
        },
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_LESS,
            .enable_depth_test = true,
            .enable_depth_write = true,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){
                {
                    .format = result->swapchain_format,
                },
            },
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
            .has_depth_stencil_target = true,
        },
    });
    NC_CHECK_SDL_RESULT(result->terrain_pipeline);

    SDL_ReleaseGPUShader(result->gpu_device, fragment_shader);
    fragment_shader = NULL;
    SDL_ReleaseGPUShader(result->gpu_device, vertex_shader);
    vertex_shader = NULL;

    vertex_shader = nc__renderer_load_shader(
            result,
            NC__RENDERER_ASTC_TEXTURES ? "shaders/gui-vert.spv" : "assets/shaders/gui-vert.spv",
            SDL_GPU_SHADERSTAGE_VERTEX,
            0,
            1);
    fragment_shader = nc__renderer_load_shader(
            result,
            NC__RENDERER_ASTC_TEXTURES ? "shaders/gui-frag.spv" : "assets/shaders/gui-frag.spv",
            SDL_GPU_SHADERSTAGE_FRAGMENT,
            1,
            0);
    NC_CHECK_RESULT(vertex_shader && fragment_shader, "Failed to load the GUI shaders.");

    result->gui_pipeline = SDL_CreateGPUGraphicsPipeline(result->gpu_device, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]){
                {
                    .slot = 0,
                    .pitch = sizeof(float) * 4 + sizeof(uint8_t) * 4,
                    .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                },
            },
            .num_vertex_buffers = 1,
            .vertex_attributes = (SDL_GPUVertexAttribute[]){
                {
                    .location = 0,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    .offset = 0,
                },
                {
                    .location = 1,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    .offset = sizeof(float) * 2,
                },
                {
                    .location = 2,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                    .offset = sizeof(float) * 4,
                },
            },
            .num_vertex_attributes = 3,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_clip = true,
        },
        .multisample_state = {
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
        },
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
            .enable_depth_test = false,
            .enable_depth_write = false,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){
                {
                    .format = result->swapchain_format,
                    .blend_state = {
                        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                        .color_blend_op = SDL_GPU_BLENDOP_ADD,
                        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                        .enable_blend = true,
                    },
                },
            },
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
            .has_depth_stencil_target = true,
        },
    });
    NC_CHECK_SDL_RESULT(result->gui_pipeline);

    SDL_ReleaseGPUShader(result->gpu_device, fragment_shader);
    SDL_ReleaseGPUShader(result->gpu_device, vertex_shader);
    SDL_DestroyProperties(props);
    return result;

error:
    SDL_DestroyProperties(props);
    SDL_ReleaseGPUShader(result->gpu_device, fragment_shader);
    SDL_ReleaseGPUShader(result->gpu_device, vertex_shader);
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
                return nc__renderer_create_depth_texture(renderer);
            }
            break;
        default:
            break;
    }

    return true;
}

bool nc_renderer_begin_frame(nc_renderer_t* renderer) {
    NC_ASSERT(!renderer->frame_command_buffer);

    renderer->frame_id++;
    renderer->frame_command_buffer = SDL_AcquireGPUCommandBuffer(renderer->gpu_device);
    NC_CHECK_SDL_RESULT(renderer->frame_command_buffer);

    const bool sdl_result = SDL_WaitAndAcquireGPUSwapchainTexture(
            renderer->frame_command_buffer,
            renderer->window,
            &renderer->frame_swapchain_texture,
            NULL,
            NULL);
    NC_CHECK_SDL_RESULT(sdl_result);

    return true;

error:
    SDL_CancelGPUCommandBuffer(renderer->frame_command_buffer);
    renderer->frame_command_buffer = NULL;
    return false;
}

bool nc_renderer_end_frame(nc_renderer_t* renderer) {
    NC_ASSERT(renderer->frame_command_buffer);

    const bool submit_result = SDL_SubmitGPUCommandBuffer(renderer->frame_command_buffer);
    renderer->frame_command_buffer = NULL;
    renderer->frame_swapchain_texture = NULL;
    NC_CHECK_SDL_RESULT(submit_result);

    return true;

error:
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
    const nc_renderer_buffer_kind_t kind,
    const uint32_t initial_capacity
) {
    static const uint8_t buffer_usage[] = {
        [NC_RENDERER_BUFFER_KIND_TERRAIN_INSTANCES] = SDL_GPU_BUFFERUSAGE_VERTEX,
        [NC_RENDERER_BUFFER_KIND_GUI_VERTICES] = SDL_GPU_BUFFERUSAGE_VERTEX,
        [NC_RENDERER_BUFFER_KIND_GUI_INDICES] = SDL_GPU_BUFFERUSAGE_INDEX,
    };

    NC_ASSERT(kind > 0 && kind <= NC_RENDERER_BUFFER_KIND_MAX);

    nc_renderer_buffer_t* result = malloc(sizeof(*result));
    *result = (nc_renderer_buffer_t){
        .gpu_buffer = SDL_CreateGPUBuffer(renderer->gpu_device, &(SDL_GPUBufferCreateInfo){
            .usage = buffer_usage[kind],
            .size = initial_capacity,
        }),
        .usage = buffer_usage[kind],
        .capacity = initial_capacity,
    };
    NC_CHECK_SDL_RESULT(result->gpu_buffer);

    return result;

error:
    free(result);
    return NULL;
}

void nc_renderer_destroy_buffer(nc_renderer_t* renderer, nc_renderer_buffer_t* buffer) {
    if (!buffer) {
        return;
    }

    SDL_ReleaseGPUBuffer(renderer->gpu_device, buffer->gpu_buffer);
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
        SDL_GPUBuffer* new_gpu_buffer = SDL_CreateGPUBuffer(renderer->gpu_device, &(SDL_GPUBufferCreateInfo){
            .usage = buffer->usage,
            .size = new_capacity,
        });
        NC_CHECK_SDL_RESULT(new_gpu_buffer);

        SDL_ReleaseGPUBuffer(renderer->gpu_device, buffer->gpu_buffer);
        buffer->gpu_buffer = new_gpu_buffer;
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
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            width,
            height,
            1,
            false);
    if (!result) {
        return NULL;
    }

    const uint32_t size = width * height * 4;
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

    const bool upload_result = nc__renderer_queue_texture_upload(renderer, texture, 0, texture_data.bytes, texture_data.size);
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

    if (!renderer->frame_swapchain_texture) {
        // This is valid: The window is minimized or something. In this case, do not draw.
        return true;
    }

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
            renderer->frame_command_buffer,
            &(SDL_GPUColorTargetInfo){
                .texture = renderer->frame_swapchain_texture,
                .clear_color = { NC__RENDERER_CLEAR_RED, NC__RENDERER_CLEAR_GREEN, NC__RENDERER_CLEAR_BLUE, 1.0f },
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
            },
            1,
            &(SDL_GPUDepthStencilTargetInfo){
                .texture = renderer->depth_texture,
                .clear_depth = 1.0f,
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_DONT_CARE,
                .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
                .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
                .cycle = true,
            });
    NC_CHECK_SDL_RESULT(render_pass);

    for (uint32_t i = 0; i < frame->opaque_draw_count; i++) {
        nc__renderer_draw_opaque(renderer, render_pass, &frame->opaque_draws[i]);
    }
    for (uint32_t i = 0; i < frame->overlay_draw_count; i++) {
        nc__renderer_draw_overlay(renderer, render_pass, &frame->overlay_draws[i]);
    }

    SDL_EndGPURenderPass(render_pass);
    return true;

error:
    return false;
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

void nc_renderer_fini(nc_renderer_t* renderer) {
    if (!renderer) {
        return;
    }

    SDL_CancelGPUCommandBuffer(renderer->frame_command_buffer);

    if (renderer->gpu_device) {
        if (renderer->mapped_transfer_buffer) {
            SDL_UnmapGPUTransferBuffer(renderer->gpu_device, renderer->transfer_buffer);
        }

        SDL_ReleaseGPUGraphicsPipeline(renderer->gpu_device, renderer->gui_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(renderer->gpu_device, renderer->terrain_pipeline);
        SDL_ReleaseGPUSampler(renderer->gpu_device, renderer->gui_sampler);
        SDL_ReleaseGPUSampler(renderer->gpu_device, renderer->terrain_sampler);
        SDL_ReleaseGPUTransferBuffer(renderer->gpu_device, renderer->transfer_buffer);
        SDL_ReleaseGPUTexture(renderer->gpu_device, renderer->depth_texture);
        SDL_ReleaseWindowFromGPUDevice(renderer->gpu_device, renderer->window);
    }

    free(renderer->upload_ops);

    SDL_DestroyWindow(renderer->window);
    SDL_DestroyGPUDevice(renderer->gpu_device);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    free(renderer);
}
