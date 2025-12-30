#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CVKM_LH
#include <cvkm.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <vulkan/vulkan.h>

#include <novacube/version.h>

typedef uint8_t nc__block_type;

enum {
    NC__BLOCK_TYPE_AIR = 0,
    NC__BLOCK_TYPE_STONE = 1,
    NC__BLOCK_TYPE_DIRT = 2,
    NC__BLOCK_TYPE_GRASS = 3,
    NC__BLOCK_TYPE_COUNT = 3,
};

#ifdef ANDROID
#define NC__ASSETS_BASE_PATH ""
#define NC__TEXTURE_FILE_EXTENSION ".astc"
#else
#define NC__ASSETS_BASE_PATH "assets/"
#define NC__TEXTURE_FILE_EXTENSION ".png"
#endif

static const char* nc__terrain_texture_paths[] = {
    NC__ASSETS_BASE_PATH "textures/stone" NC__TEXTURE_FILE_EXTENSION,
    NC__ASSETS_BASE_PATH "textures/dirt" NC__TEXTURE_FILE_EXTENSION,
    NC__ASSETS_BASE_PATH "textures/grass" NC__TEXTURE_FILE_EXTENSION,
};

typedef struct nc__block_t {
    vkm_ubvec3 position;
    nc__block_type type;
} nc__block_t;

typedef struct nc__camera_t {
    vkm_vec3 position;
    float yaw, pitch;
} nc__camera_t;

typedef struct nc__astc_header {
    uint8_t magic[4];
    uint8_t block_x;
    uint8_t block_y;
    uint8_t block_z;
    uint8_t dim_x[3];
    uint8_t dim_y[3];
    uint8_t dim_z[3];
} nc__astc_header;

typedef struct nc__touch_event_t {
    vkm_vec2 initial_pos, current_pos;
    SDL_FingerID finger_id;
    Uint64 timestamp;
} nc__touch_event_t;

#ifndef ANDROID
#define NC__BACKGROUND_DELAY 100
#endif
#define NC__CHUNK_LENGTH 256
#define NC__CHUNK_COUNT (NC__CHUNK_LENGTH * NC__CHUNK_LENGTH * NC__CHUNK_LENGTH)
#define NC__CHUNK_SIZE (NC__CHUNK_COUNT * sizeof(nc__block_t))
#define NC__CHUNK_INDEX(x, y, z) ((x) + ((y) * NC__CHUNK_LENGTH) + ((z) * NC__CHUNK_LENGTH * NC__CHUNK_LENGTH))
#define NC__MOUSE_SENSITIVITY vkm_deg2rad(1.0f)
#define NC__TOUCHSCREEN_SENSITIVITY 15.0f
#define NC__MOVEMENT_SPEED 5.0f
#define NC__COUNTOF(a) (sizeof(a) / sizeof(*a))
#define NC__TERRAIN_TEXTURE_LENGTH 16
#ifdef ANDROID
// astc 4x4: 1 byte per texel
#define NC__TERRAIN_TEXTURE_SIZE (NC__TERRAIN_TEXTURE_LENGTH * NC__TERRAIN_TEXTURE_LENGTH)
#else
#define NC__TERRAIN_TEXTURE_SIZE (NC__TERRAIN_TEXTURE_LENGTH * NC__TERRAIN_TEXTURE_LENGTH * 4)
#endif
#ifdef NDEBUG
#define NC__BUILD_TYPE "Release"
#else
#define NC__BUILD_TYPE "Debug"
#endif

#define NC__CHECK_SDL_RESULT(result) do { \
    if (!result) { \
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", SDL_GetError(), NULL); \
        goto error; \
    } \
} while (false)

static SDL_GPUDevice* nc__gpu_device;
static SDL_Window* nc__window;
static SDL_GPUTexture* nc__depth_texture;
static vkm_usvec2 nc__viewport_size;
#define TDS_VALUE_T nc__block_t
#define TDS_TYPE nc__block_dense_pool_t
#define TDS_INITIAL_CAPACITY NC__CHUNK_COUNT
#include <tds/dense-pool.h>
static nc__block_dense_pool_t nc__chunk;
static bool nc__foreground = true;
static SDL_GPUBuffer* nc__vertex_buffer;
static SDL_GPUTransferBuffer* nc__transfer_buffer;
static nc__camera_t nc__camera = {
    .position = { { 127.5f, 127.5f, 124.0f } },
};
static const bool* nc__keyboard_state;
static SDL_GPUTexture* nc__terrain_textures;
static SDL_GPUSampler* nc__texture_sampler;
static SDL_GPUGraphicsPipeline* nc__pipeline;
static nc__touch_event_t nc__move_touch, nc__look_touch;
static nc__block_type selected_type = NC__BLOCK_TYPE_STONE;

static bool nc__load_astc_header(const char* data, nc__astc_header* header) {
    if (*(uint32_t*)data != 0x5ca1ab13) {
        return SDL_SetError("Invalid ASTC header.");
    }

    memcpy(header, data, sizeof(*header));
    return true;
}

// TODO: Review for memory correctness.
static bool nc__load_texture(const char* path, char* data, const unsigned index) {
#ifdef ANDROID
    size_t size;
    char* file_data = SDL_LoadFile(path, &size);
    if (!file_data) {
        return false;
    }
    if (size < sizeof(nc__astc_header)) {
        SDL_free(file_data);
        return false;
    }

    nc__astc_header header;
    if (!nc__load_astc_header(file_data, &header)) {
        SDL_free(file_data);
        return false;
    }

    assert(header.block_x == 4);
    assert(header.block_y == 4);
    assert(header.block_z == 1);
    // max 65535x65535 image
    assert(!header.dim_x[2] && !header.dim_y[2]);
    // z dimension must be 1 for 2D images
    assert(header.dim_z[0] == 1 && !header.dim_z[1] && !header.dim_z[2]);

#ifndef NDEBUG
    const uint16_t width = *(uint16_t*)header.dim_x;
    assert(!header.dim_x[2]);
    const uint16_t height = *(uint16_t*)header.dim_y;
    assert(!header.dim_y[2]);
    assert(width == height && width == NC__TERRAIN_TEXTURE_LENGTH);
#endif

    memcpy(data + NC__TERRAIN_TEXTURE_SIZE * index, file_data + sizeof(header), NC__TERRAIN_TEXTURE_SIZE);
    SDL_free(file_data);

    return true;
#else
    SDL_Surface* texture = SDL_LoadPNG(path);
    if (!texture) {
        return false;
    }
    assert(texture->format == SDL_PIXELFORMAT_RGBA32);
    assert(texture->w == texture->h && texture->w == NC__TERRAIN_TEXTURE_LENGTH);

    memcpy(data + NC__TERRAIN_TEXTURE_SIZE * index, texture->pixels, NC__TERRAIN_TEXTURE_SIZE);
    SDL_DestroySurface(texture);

    return true;
#endif
}

static SDL_GPUShader* nc__load_shader(
    const char* path,
    const SDL_GPUShaderStage stage,
    const Uint32 sampler_count,
    const Uint32 uniform_buffer_count,
    const Uint32 storage_buffer_count,
    const Uint32 storage_texture_count
) {
    void* code = NULL;
    SDL_GPUShader* result = NULL;

    size_t code_size;
    code = SDL_LoadFile(path, &code_size);
    if (!code) {
        return NULL;
    }

    result = SDL_CreateGPUShader(nc__gpu_device, &(SDL_GPUShaderCreateInfo){
        .code_size = code_size,
        .code = code,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = stage,
        .num_samplers = sampler_count,
        .num_storage_textures = storage_texture_count,
        .num_storage_buffers = storage_buffer_count,
        .num_uniform_buffers = uniform_buffer_count,
    });

    SDL_free(code);
    return result;
}

SDL_AppResult SDL_AppInit(void** app_state, const int argc, char** argv) {
    (void)app_state;
    (void)argc;
    (void)argv;

    SDL_PropertiesID props = 0;
    SDL_GPUTransferBuffer* transfer_buffer = NULL;
    SDL_GPUCommandBuffer* command_buffer = NULL;
    SDL_GPUShader* vertex_shader = NULL, *fragment_shader = NULL;

    SDL_Log("Novacube " NC__VERSION "\nBuild: " __DATE__ " " __TIME__ " " NC__BUILD_TYPE "\nGit: " NC__GIT_DESCRIBE "\nCommit: " NC__GIT_HASH);

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    bool sdl_result = SDL_InitSubSystem(SDL_INIT_VIDEO);
    NC__CHECK_SDL_RESULT(sdl_result);

    props = SDL_CreateProperties();
    NC__CHECK_SDL_RESULT(props);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
#ifndef NDEBUG
            true);
#else
            false);
#endif
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,
            true);
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, true);
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
    NC__CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
    NC__CHECK_SDL_RESULT(sdl_result);
    SDL_GPUVulkanOptions options = {
        .vulkan_api_version = VK_API_VERSION_1_0,
    };
    sdl_result = SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &options);
    NC__CHECK_SDL_RESULT(sdl_result);
    nc__gpu_device = SDL_CreateGPUDeviceWithProperties(props);
    NC__CHECK_SDL_RESULT(nc__gpu_device);
    SDL_DestroyProperties(props);

    props = SDL_GetGPUDeviceProperties(nc__gpu_device);
    SDL_Log("%s", SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "Unknown GPU"));

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#ifdef ANDROID
    // Android always has a fullscreen
    flags |= SDL_WINDOW_FULLSCREEN;
#endif

    nc__window = SDL_CreateWindow("Novacube " NC__VERSION, 640, 480, flags);
    NC__CHECK_SDL_RESULT(nc__window);
    // get the actual window dimensions
    int width, height;
    sdl_result = SDL_GetWindowSize(nc__window, &width, &height);
    NC__CHECK_SDL_RESULT(sdl_result);
    nc__viewport_size.x = (uint16_t)width;
    nc__viewport_size.y = (uint16_t)height;

    sdl_result = SDL_ClaimWindowForGPUDevice(nc__gpu_device, nc__window);
    NC__CHECK_SDL_RESULT(sdl_result);

    nc__depth_texture = SDL_CreateGPUTexture(nc__gpu_device, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    });
    NC__CHECK_SDL_RESULT(nc__depth_texture);

    for (int z = 126; z < 129; z++) {
        for (int y = 126; y < 129; y++) {
            for (int x = 126; x < 129; x++) {
                nc__block_dense_pool_t_append(&nc__chunk, (nc__block_t){
                    .position = { { (uint8_t)x, (uint8_t)y, (uint8_t)z } },
                    .type = y == 126 ? NC__BLOCK_TYPE_STONE : y == 127 ? NC__BLOCK_TYPE_DIRT : NC__BLOCK_TYPE_GRASS,
                });
            }
        }
    }

    nc__vertex_buffer = SDL_CreateGPUBuffer(nc__gpu_device, &(SDL_GPUBufferCreateInfo){
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = NC__CHUNK_SIZE,
    });
    NC__CHECK_SDL_RESULT(nc__vertex_buffer);
    nc__transfer_buffer = SDL_CreateGPUTransferBuffer(nc__gpu_device, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = NC__CHUNK_SIZE,
    });
    NC__CHECK_SDL_RESULT(nc__transfer_buffer);

    nc__terrain_textures = SDL_CreateGPUTexture(nc__gpu_device, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D_ARRAY,
#ifdef ANDROID
        .format = SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM,
#else
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
#endif
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = 16,
        .height = 16,
        .layer_count_or_depth = NC__BLOCK_TYPE_COUNT,
        .num_levels = 1,
    });

    nc__texture_sampler = SDL_CreateGPUSampler(nc__gpu_device, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
    });

    transfer_buffer = SDL_CreateGPUTransferBuffer(nc__gpu_device, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = NC__COUNTOF(nc__terrain_texture_paths) * NC__TERRAIN_TEXTURE_SIZE,
    });
    NC__CHECK_SDL_RESULT(transfer_buffer);

    char* mapped = SDL_MapGPUTransferBuffer(nc__gpu_device, transfer_buffer, false);
    NC__CHECK_SDL_RESULT(mapped);
    for (unsigned i = 0; i < NC__COUNTOF(nc__terrain_texture_paths); i++) {
        const bool result = nc__load_texture(nc__terrain_texture_paths[i], mapped, i);
        NC__CHECK_SDL_RESULT(result);
    }
    SDL_UnmapGPUTransferBuffer(nc__gpu_device, transfer_buffer);

    command_buffer = SDL_AcquireGPUCommandBuffer(nc__gpu_device);
    NC__CHECK_SDL_RESULT(command_buffer);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    for (unsigned i = 0; i < NC__COUNTOF(nc__terrain_texture_paths); i++) {
        SDL_UploadToGPUTexture(
                copy_pass,
                &(SDL_GPUTextureTransferInfo){
                    .transfer_buffer = transfer_buffer,
                    .offset = i * NC__TERRAIN_TEXTURE_SIZE,
                },
                &(SDL_GPUTextureRegion){
                    .texture = nc__terrain_textures,
                    .layer = i,
                    .w = NC__TERRAIN_TEXTURE_LENGTH,
                    .h = NC__TERRAIN_TEXTURE_LENGTH,
                    .d = 1,
                },
                false);
    }
    SDL_EndGPUCopyPass(copy_pass);

    sdl_result = SDL_SubmitGPUCommandBuffer(command_buffer);
    command_buffer = NULL;
    NC__CHECK_SDL_RESULT(sdl_result);

    SDL_ReleaseGPUTransferBuffer(nc__gpu_device, transfer_buffer);
    transfer_buffer = NULL;

    SDL_Log("Loaded %d terrain textures.", (int)NC__COUNTOF(nc__terrain_texture_paths));

    vertex_shader = nc__load_shader(NC__ASSETS_BASE_PATH "shaders/cube-vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    NC__CHECK_SDL_RESULT(vertex_shader);
    fragment_shader = nc__load_shader(NC__ASSETS_BASE_PATH "shaders/cube-frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 0, 0);
    NC__CHECK_SDL_RESULT(fragment_shader);

    nc__pipeline = SDL_CreateGPUGraphicsPipeline(nc__gpu_device, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]){
                {
                    .slot = 0,
                    .pitch = sizeof(nc__block_t),
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
                    .format = SDL_GetGPUSwapchainTextureFormat(nc__gpu_device, nc__window),
                },
            },
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
            .has_depth_stencil_target = true,
        },
    });
    NC__CHECK_SDL_RESULT(nc__pipeline);
    SDL_ReleaseGPUShader(nc__gpu_device, vertex_shader);
    vertex_shader = NULL;
    SDL_ReleaseGPUShader(nc__gpu_device, fragment_shader);
    fragment_shader = NULL;

    nc__keyboard_state = SDL_GetKeyboardState(NULL);

    SDL_SetWindowRelativeMouseMode(nc__window, true);

    return SDL_APP_CONTINUE;

    error:
    SDL_ReleaseGPUGraphicsPipeline(nc__gpu_device, nc__pipeline);
    nc__pipeline = NULL;
    SDL_ReleaseGPUShader(nc__gpu_device, fragment_shader);
    fragment_shader = NULL;
    SDL_ReleaseGPUShader(nc__gpu_device, vertex_shader);
    vertex_shader = NULL;
    SDL_CancelGPUCommandBuffer(command_buffer);
    command_buffer = NULL;
    SDL_ReleaseGPUTransferBuffer(nc__gpu_device, transfer_buffer);
    transfer_buffer = NULL;
    SDL_ReleaseGPUSampler(nc__gpu_device, nc__texture_sampler);
    nc__texture_sampler = NULL;
    SDL_ReleaseGPUTexture(nc__gpu_device, nc__terrain_textures);
    nc__terrain_textures = NULL;
    SDL_ReleaseGPUTransferBuffer(nc__gpu_device, nc__transfer_buffer);
    nc__transfer_buffer = NULL;
    SDL_ReleaseGPUBuffer(nc__gpu_device, nc__vertex_buffer);
    nc__vertex_buffer = NULL;
    nc__block_dense_pool_t_fini(&nc__chunk);
    SDL_ReleaseGPUTexture(nc__gpu_device, nc__depth_texture);
    nc__depth_texture = NULL;
    SDL_ReleaseWindowFromGPUDevice(nc__gpu_device, nc__window);
    SDL_DestroyWindow(nc__window);
    nc__window = NULL;
    SDL_DestroyGPUDevice(nc__gpu_device);
    nc__gpu_device = NULL;
    SDL_DestroyProperties(props);
    props = 0;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return SDL_APP_FAILURE;
}

// Pass the air block to remove the block instead of placing one.
static void nc__modify_block(const nc__block_type new_block) {
    // https://tavianator.com/2011/ray_box.html
    // https://tavianator.com/cgit/dimension.git/tree/libdimension/bvh/bvh.c#n178
    // struct dmnsn_optimized_ray and dmnsn_ray_box_intersection()
    const float pitch_cosine = vkm_cos(nc__camera.pitch);
    vkm_vec3 inverse_ray_direction = { {
        1.0f / (pitch_cosine * vkm_sin(nc__camera.yaw)),
        1.0f / vkm_sin(nc__camera.pitch),
        1.0f / (pitch_cosine * vkm_cos(nc__camera.yaw)),
    } };

    float closest_distance = INFINITY;
    uint32_t closest_block_id = UINT32_MAX;
    vkm_bvec3 normal = { 0 };
    for (uint32_t i = 0; i < nc__chunk.count; i++) {
        const nc__block_t block = nc__chunk.array[i];

        const vkm_vec3 box_min = { { block.position.x, block.position.y, block.position.z } };
        const vkm_vec3 box_max = { { box_min.x + 1.0f, box_min.y + 1.0f, box_min.z + 1.0f } };

        vkm_vec3 t0;
        vkm_sub(&box_min, &nc__camera.position, &t0);
        vkm_mul(&t0, &inverse_ray_direction, &t0);

        vkm_vec3 t1;
        vkm_sub(&box_max, &nc__camera.position, &t1);
        vkm_mul(&t1, &inverse_ray_direction, &t1);

        vkm_vec3 enter_distances;
        vkm_min(&t0, &t1, &enter_distances);

        vkm_vec3 exit_distances;
        vkm_max(&t0, &t1, &exit_distances);

        const float enter_distance = vkm_scalar_max(&enter_distances);
        const float exit_distance = vkm_scalar_min(&exit_distances);

        if (exit_distance >= vkm_max(enter_distance, 0.0f)) {
            // When the distance is negative, the intersection is behind the camera. That is, we are inside the box.
            // In this case, I decided to report a distance of 0.
            // We could also report the negative distance to know how deep we are inside the box,
            // or the exit distance to know how far we are from exiting.
            // Or whichever is closer to the AABB boundaries.
            const float hit_distance = vkm_max(enter_distance, 0.0f);
            if (hit_distance < closest_distance) {
                closest_distance = hit_distance;
                closest_block_id = nc__chunk.dense[i];
                normal = (vkm_bvec3){ {
                    (int8_t)((enter_distance == enter_distances.x) * (inverse_ray_direction.x < 0.0f ? 1 : -1)),
                    (int8_t)((enter_distance == enter_distances.y) * (inverse_ray_direction.y < 0.0f ? 1 : -1)),
                    (int8_t)((enter_distance == enter_distances.z) * (inverse_ray_direction.z < 0.0f ? 1 : -1)),
                } };
            }
        }
    }

    if (closest_distance == INFINITY) {
        return;
    }

    if (new_block == NC__BLOCK_TYPE_AIR) {
        nc__block_dense_pool_t_remove(&nc__chunk, closest_block_id);
    } else if (closest_distance > 1.0f) {
        const vkm_ubvec3 closest_block_position = nc__block_dense_pool_t_get(&nc__chunk, closest_block_id).position;
        nc__block_t appended_block = {
            .position = { {
                closest_block_position.x + normal.x,
                closest_block_position.y + normal.y,
                closest_block_position.z + normal.z,
            } },
            .type = new_block,
        };
        nc__block_dense_pool_t_append(&nc__chunk, appended_block);
    }
}

SDL_AppResult SDL_AppIterate(void* app_state) {
    (void)app_state;

    SDL_GPUCommandBuffer* command_buffer = NULL;
    SDL_GPUCopyPass* copy_pass = NULL;

    const Uint64 ticks = SDL_GetTicksNS();
    static Uint64 last_ticks = 0;
    const double delta_time = last_ticks == 0 ? 1.0 / 60.0 : (double)(ticks - last_ticks) / 1000000000.0;
    last_ticks = ticks;

    if (nc__look_touch.finger_id) {
        vkm_vec2 delta;
        vkm_sub(&nc__look_touch.current_pos, &nc__look_touch.initial_pos, &delta);
        nc__camera.yaw += delta.x * NC__TOUCHSCREEN_SENSITIVITY * (float)delta_time;
        nc__camera.pitch += -delta.y * NC__TOUCHSCREEN_SENSITIVITY * (float)delta_time;
    }

    const float pitch_sine = vkm_sin(nc__camera.pitch);
    const float pitch_cosine = vkm_cos(nc__camera.pitch);
    const float yaw_sine = vkm_sin(nc__camera.yaw);
    const float yaw_cosine = vkm_cos(nc__camera.yaw);
    const vkm_vec3 forward = { {
        pitch_cosine * yaw_sine,
        pitch_sine,
        pitch_cosine * yaw_cosine,
    } };
    vkm_vec3 right, up;
    vkm_vec3_cross(&CVKM_VEC3_UP, &forward, &right);
    vkm_vec3_normalize(&right, &right);
    vkm_vec3_cross(&forward, &right, &up);

    vkm_vec3 input = { {
        (float)nc__keyboard_state[SDL_SCANCODE_D] - (float)nc__keyboard_state[SDL_SCANCODE_A],
        (float)nc__keyboard_state[SDL_SCANCODE_R] - (float)nc__keyboard_state[SDL_SCANCODE_F],
        (float)nc__keyboard_state[SDL_SCANCODE_W] - (float)nc__keyboard_state[SDL_SCANCODE_S],
    } };

    if (nc__move_touch.finger_id) {
        vkm_vec2 delta;
        vkm_sub(&nc__move_touch.current_pos, &nc__move_touch.initial_pos, &delta);
        // emulate touchscreen analog input that doesn't take the entire screen to make values go anywhere from 0 to 1.
        vkm_mul(&delta, 10.0f, &delta);
        input.x += delta.x;
        input.z += -delta.y;
    }

    const float length = vkm_length(&input);
    if (length > 1.0f) {
        vkm_div(&input, length, &input);
    }

    vkm_vec3 velocity;
    vkm_mul(&right, input.x, &velocity);
    vkm_muladd(&up, input.y, &velocity);
    vkm_muladd(&forward, input.z, &velocity);

    vkm_mul(&velocity, NC__MOVEMENT_SPEED, &velocity);

    vkm_muladd(&velocity, (float)delta_time, &nc__camera.position);

    vkm_mat4 view_matrix;
    vkm_vec3 target;
    vkm_vec3_add(&nc__camera.position, &forward, &target);
    vkm_look_at(&nc__camera.position, &target, &CVKM_VEC3_UP, &view_matrix);

    vkm_mat4 projection;
    vkm_perspective(
            vkm_deg2rad(80.0f),
            (float)nc__viewport_size.x / (float)nc__viewport_size.y,
            0.2f,
            500.0f,
            &projection);

    vkm_mat4 view_projection;
    vkm_mul(&projection, &view_matrix, &view_projection);

#ifndef ANDROID
    if (!nc__foreground) {
        SDL_Delay(NC__BACKGROUND_DELAY);
    }
#endif

    command_buffer = SDL_AcquireGPUCommandBuffer(nc__gpu_device);
    NC__CHECK_SDL_RESULT(command_buffer);

    nc__block_t* mapped = SDL_MapGPUTransferBuffer(nc__gpu_device, nc__transfer_buffer, true);
    NC__CHECK_SDL_RESULT(mapped);
    memcpy(mapped, nc__chunk.array, nc__chunk.count * sizeof(*nc__chunk.array));
    SDL_UnmapGPUTransferBuffer(nc__gpu_device, nc__transfer_buffer);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_UploadToGPUBuffer(
            copy_pass,
            &(SDL_GPUTransferBufferLocation){
                .transfer_buffer = nc__transfer_buffer,
                .offset = 0,
            },
            &(SDL_GPUBufferRegion){
                .buffer = nc__vertex_buffer,
                .offset = 0,
                .size = NC__CHUNK_SIZE,
            },
            true);
    SDL_EndGPUCopyPass(copy_pass);
    copy_pass = NULL;

    SDL_GPUTexture* swapchain_texture;
    bool sdl_result = SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, nc__window, &swapchain_texture, NULL, NULL);
    NC__CHECK_SDL_RESULT(sdl_result);
    if (swapchain_texture) {
        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
                command_buffer,
                &(SDL_GPUColorTargetInfo){
                    .texture = swapchain_texture,
                    .clear_color = { 0.53f, 0.81f, 0.92f, 1.0f },
                    .load_op = SDL_GPU_LOADOP_CLEAR,
                    .store_op = SDL_GPU_STOREOP_STORE,
                    .cycle = true,
                },
                1,
                &(SDL_GPUDepthStencilTargetInfo){
                    .texture = nc__depth_texture,
                    .clear_depth = 1.0f,
                    .load_op = SDL_GPU_LOADOP_CLEAR,
                    .store_op = SDL_GPU_STOREOP_DONT_CARE,
                    .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
                    .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
                    .cycle = true,
                });
        SDL_BindGPUGraphicsPipeline(render_pass, nc__pipeline);
        SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){ .buffer = nc__vertex_buffer, .offset = 0 }, 1);
        SDL_BindGPUFragmentSamplers(
                render_pass,
                0,
                &(SDL_GPUTextureSamplerBinding){
                    .texture = nc__terrain_textures,
                    .sampler = nc__texture_sampler,
                },
                1);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &view_projection, sizeof(view_projection));
        SDL_DrawGPUPrimitives(render_pass, 36, nc__chunk.count, 0, 0);
        SDL_EndGPURenderPass(render_pass);
    }

    sdl_result = SDL_SubmitGPUCommandBuffer(command_buffer);
    NC__CHECK_SDL_RESULT(sdl_result);
    return SDL_APP_CONTINUE;

    error:
    if (copy_pass) {
        SDL_EndGPUCopyPass(copy_pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", SDL_GetError(), NULL);
    }
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void* app_state, SDL_Event* event) {
    (void)app_state;

    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_MINIMIZED:
            nc__foreground = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            // recreate depth texture with new dimensions
            SDL_ReleaseGPUTexture(nc__gpu_device, nc__depth_texture);
            nc__viewport_size = (vkm_usvec2){
                .x = (uint16_t)event->window.data1,
                .y = (uint16_t)event->window.data2,
            };
            nc__depth_texture = SDL_CreateGPUTexture(nc__gpu_device, &(SDL_GPUTextureCreateInfo){
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                .width = nc__viewport_size.x,
                .height = nc__viewport_size.y,
                .layer_count_or_depth = 1,
                .num_levels = 1,
                .sample_count = SDL_GPU_SAMPLECOUNT_1,
            });
            NC__CHECK_SDL_RESULT(nc__depth_texture);
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            nc__foreground = true;
            break;
        case SDL_EVENT_QUIT:
            SDL_Log("Received a quit event.");
            nc__foreground = false;
            return SDL_APP_SUCCESS;
        case SDL_EVENT_TERMINATING:
            SDL_Log("Received a terminating event.");
            nc__foreground = false;
            return SDL_APP_SUCCESS;
        case SDL_EVENT_MOUSE_MOTION:
            nc__camera.yaw = vkm_mod(nc__camera.yaw + event->motion.xrel * NC__MOUSE_SENSITIVITY, 2.0f * CVKM_PI_F);
            nc__camera.pitch = vkm_clamp(
                    nc__camera.pitch - event->motion.yrel * NC__MOUSE_SENSITIVITY,
                    -CVKM_PI_2_F + 0.001f,
                    CVKM_PI_2_F - 0.001f);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                SDL_SetWindowRelativeMouseMode(nc__window, false);
            } else if (event->key.scancode >= SDL_SCANCODE_1 && event->key.scancode <= SDL_SCANCODE_0) {
                selected_type = ((event->key.scancode - SDL_SCANCODE_1) % NC__BLOCK_TYPE_COUNT) + 1;
            }
            break;
        case SDL_EVENT_FINGER_DOWN:
            if (event->tfinger.x < 0.5f) {
                if (!nc__move_touch.finger_id) {
                    nc__move_touch = (nc__touch_event_t){
                        .initial_pos = { { event->tfinger.x, event->tfinger.y } },
                        .current_pos = { { event->tfinger.x, event->tfinger.y } },
                        .finger_id = event->tfinger.fingerID,
                        .timestamp = event->tfinger.timestamp,
                    };
                }
            } else {
                if (!nc__look_touch.finger_id) {
                    nc__look_touch = (nc__touch_event_t){
                        .initial_pos = { { event->tfinger.x, event->tfinger.y } },
                        .current_pos = { { event->tfinger.x, event->tfinger.y } },
                        .finger_id = event->tfinger.fingerID,
                        .timestamp = event->tfinger.timestamp,
                    };
                }
            }
            break;
        case SDL_EVENT_FINGER_UP:
            if (nc__move_touch.finger_id == event->tfinger.fingerID) {
                nc__move_touch.finger_id = 0;
                if (event->tfinger.timestamp - nc__move_touch.timestamp < 500000000) {
                    nc__modify_block(NC__BLOCK_TYPE_AIR);
                }
            } else if (nc__look_touch.finger_id == event->tfinger.fingerID) {
                nc__look_touch.finger_id = 0;
                if (event->tfinger.timestamp - nc__look_touch.timestamp < 500000000) {
                    nc__modify_block(selected_type);
                }
            }
            break;
        case SDL_EVENT_FINGER_CANCELED:
            if (nc__move_touch.finger_id == event->tfinger.fingerID) {
                nc__move_touch.finger_id = 0;
            } else if (nc__look_touch.finger_id == event->tfinger.fingerID) {
                nc__look_touch.finger_id = 0;
            }
            break;
        case SDL_EVENT_FINGER_MOTION:
            if (nc__move_touch.finger_id == event->tfinger.fingerID) {
                nc__move_touch.current_pos = (vkm_vec2){{event->tfinger.x, event->tfinger.y } };
            } else if (nc__look_touch.finger_id == event->tfinger.fingerID) {
                nc__look_touch.current_pos = (vkm_vec2){{event->tfinger.x, event->tfinger.y } };
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                nc__modify_block(selected_type);
            }
            if (event->button.button == SDL_BUTTON_RIGHT) {
                nc__modify_block(NC__BLOCK_TYPE_AIR);
            }
            break;
        default:
            break;
    }

    return SDL_APP_CONTINUE;

    error:
    return SDL_APP_FAILURE;
}

void SDL_AppQuit(void* app_state, const SDL_AppResult result) {
    (void)app_state;
    (void)result;

    SDL_Log("See you later!");

    SDL_ReleaseGPUGraphicsPipeline(nc__gpu_device, nc__pipeline);
    nc__pipeline = NULL;
    SDL_ReleaseGPUSampler(nc__gpu_device, nc__texture_sampler);
    nc__texture_sampler = NULL;
    SDL_ReleaseGPUTexture(nc__gpu_device, nc__terrain_textures);
    nc__terrain_textures = NULL;
    SDL_ReleaseGPUTransferBuffer(nc__gpu_device, nc__transfer_buffer);
    nc__transfer_buffer = NULL;
    SDL_ReleaseGPUBuffer(nc__gpu_device, nc__vertex_buffer);
    nc__vertex_buffer = NULL;
    nc__block_dense_pool_t_fini(&nc__chunk);
    SDL_ReleaseGPUTexture(nc__gpu_device, nc__depth_texture);
    nc__depth_texture = NULL;
    SDL_ReleaseWindowFromGPUDevice(nc__gpu_device, nc__window);
    SDL_DestroyWindow(nc__window);
    nc__window = NULL;
    SDL_DestroyGPUDevice(nc__gpu_device);
    nc__gpu_device = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
}
