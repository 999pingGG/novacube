#include <assert.h>
#include <string.h>

#define CLAY_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <cvkm.h>
#include <rapidhash.h>
#include <SDL3/SDL.h>

#include "novacube/sdl_util.h"
#include "novacube/ui.h"

#ifdef ANDROID
#define NC__TEXTURES_BASE_PATH "textures/"
#define NC__TEXTURE_FILE_EXTENSION ".astc"
#else
#define NC__TEXTURES_BASE_PATH "assets/textures/"
#define NC__TEXTURE_FILE_EXTENSION ".png"
#endif

#define NC__QUAD_IS_TEXT 0xb00b
#define NC__DEBUG_FONT_CHAR_WIDTH 5
#define NC__DEBUG_FONT_CHAR_HEIGHT 8
#define NC__DEBUG_FONT_CHAR_WIDTH_SIZE_FACTOR ((float)NC__DEBUG_FONT_CHAR_WIDTH / (float)NC__DEBUG_FONT_CHAR_HEIGHT)
#define NC__FONT_SIZE_ADJUSTMENT_FACTOR 1.0f
#define NC__INVALID_ID 0xffffffffu

#define TDS_TYPE nc__string_uint_hashmap
#define TDS_KEY_T char*
#define TDS_VALUE_T uint32_t
#define TDS_SIZE_T uint32_t
#define TDS_HASH_KEY(key) rapidhashNano(key, strlen(key))
#define TDS_KEY_EQUALS(a, b) (strcmp((a), (b)) == 0)
#define TDS_KEY_FINI(key) free(key)
#include <tds/hashmap.h>

#define TDS_TYPE nc__uint_string_hashmap
#define TDS_KEY_T uint32_t
#define TDS_VALUE_T char*
#define TDS_SIZE_T uint32_t
#define TDS_VALUE_EQUALS(a, b) (strcmp((a), (b)) == 0)
#define TDS_VALUE_FINI(value) free(value)
#include <tds/hashmap.h>

#define TDS_TYPE nc__pending_texture_upload_vector
#define TDS_VALUE_T uint32_t
#include <tds/vector.h>

typedef struct nc__dynamic_transfer_buffer_t {
    SDL_GPUTransferBuffer* transfer_buffer;
    char* mapped;
    unsigned offset, size;
} nc__dynamic_transfer_buffer_t;

typedef struct nc__rectangle_t {
    vkm_srect rectangle;
    vkm_ubvec4 color;
    // TODO: Make this use ushort instead of float?
    union {
        struct {
            float top_left, top_right, bottom_left, bottom_right;
        };
        vkm_vec4 vec;
    } border_radiuses;
    union {
        struct {
            uint16_t left, right, top, bottom;
        };
        vkm_usvec4 vec;
    } border_thicknesses;
    uint8_t character;
    uint8_t padding1;
    uint16_t draw_order;
    vkm_srect scissor;
} nc__rectangle_t;

typedef struct nc__quads_to_draw_t {
    SDL_GPUBuffer* buffer;
    SDL_GPUTransferBuffer* transfer_buffer;
    nc__rectangle_t* mapped;
    unsigned count, capacity;
} nc__quads_to_draw_t;

typedef struct nc__ui_image_uniforms_t {
    vkm_rect rectangle;
    vkm_vec4 color;
    union {
        struct {
            float top_left, top_right, bottom_left, bottom_right;
        };
        vkm_vec4 vec;
    } border_radiuses;
    vkm_rect scissor;
    uint32_t draw_order;
} nc__ui_image_uniforms_t;

typedef struct nc__ui_image_t {
    nc__ui_image_uniforms_t uniforms;
    unsigned id;
} nc__ui_image_t;

#define TDS_TYPE nc__ui_images_t
#define TDS_VALUE_T nc__ui_image_t
#include <tds/vector.h>

typedef struct nc__sdl_gpu_texture_t {
    SDL_GPUTexture* texture;
    vkm_usvec2 dimensions;
} nc__sdl_gpu_texture_t;

#define TDS_TYPE nc__sdl_gpu_texture_pool
#define TDS_VALUE_T nc__sdl_gpu_texture_t
#include <tds/dense-pool.h>

typedef struct nc__ui_textures_t {
    nc__string_uint_hashmap name_to_id_map;
    nc__uint_string_hashmap id_to_name_map;
    nc__sdl_gpu_texture_pool textures;
} nc__ui_textures_t;

typedef struct nc__pending_uploads_t {
    nc__pending_texture_upload_vector vector;
    nc__dynamic_transfer_buffer_t dynamic_transfer_buffer;
} nc__pending_uploads_t;

static nc__quads_to_draw_t nc__quads_to_draw;
static uint16_t nc__draw_order_counter;
static nc__pending_uploads_t nc__pending_uploads;
static nc__ui_images_t nc__ui_images;
static nc__ui_textures_t nc__ui_textures;
static Clay_Vector2 nc__scroll_delta;
static SDL_GPUGraphicsPipeline* nc__pipeline;

static unsigned nc__next_power_of_2(unsigned i) {
    i--;
    i |= i >> 1;
    i |= i >> 2;
    i |= i >> 4;
    i |= i >> 8;
    i |= i >> 16;
    i++;
    return i;
}

static uint32_t nc__load_texture(
    SDL_GPUDevice* device,
    const char* name,
    const char* path
) {
    SDL_Surface* surface = NULL;
    SDL_GPUTransferBuffer* new_transfer_buffer = NULL;

    if (nc__string_uint_hashmap_get(&nc__ui_textures.name_to_id_map, (void*)name)) {
        // TODO: Replace texture instead?
        return NC__INVALID_ID;
    }

    if (!nc__pending_uploads.dynamic_transfer_buffer.mapped) {
        nc__pending_uploads.dynamic_transfer_buffer.mapped = SDL_MapGPUTransferBuffer(
                device,
                nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer,
                true);
        NC_CHECK_SDL_RESULT(nc__pending_uploads.dynamic_transfer_buffer.mapped);
        assert(nc__pending_uploads.dynamic_transfer_buffer.offset == 0);
    }

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
    surface = SDL_LoadPNG(path);
    NC_CHECK_SDL_RESULT(surface);

    // TODO: Smaller format for monochrome or grayscale textures.
    assert(surface->format == SDL_PIXELFORMAT_RGBA32);
    assert(surface->w && surface->w <= 4096);
    assert(surface->h && surface->h <= 4096);

    // TODO: ASTC
    const unsigned texture_size = surface->w * surface->h * 4;
    const unsigned needed_size = nc__pending_uploads.dynamic_transfer_buffer.offset + texture_size;
    if (needed_size > nc__pending_uploads.dynamic_transfer_buffer.size) {
        nc__pending_uploads.dynamic_transfer_buffer.size = nc__next_power_of_2(needed_size);

        new_transfer_buffer = SDL_CreateGPUTransferBuffer(
                device,
                &(SDL_GPUTransferBufferCreateInfo){
                    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                    .size = nc__pending_uploads.dynamic_transfer_buffer.size,
                });
        NC_CHECK_SDL_RESULT(new_transfer_buffer);

        char* new_mapped = SDL_MapGPUTransferBuffer(device, new_transfer_buffer, false);
        NC_CHECK_SDL_RESULT(new_mapped);

        memcpy(
                new_mapped,
                nc__pending_uploads.dynamic_transfer_buffer.mapped,
                nc__pending_uploads.dynamic_transfer_buffer.offset);

        SDL_UnmapGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);
        SDL_ReleaseGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);

        nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer = new_transfer_buffer;
        nc__pending_uploads.dynamic_transfer_buffer.mapped = new_mapped;
    }

    memcpy(
            nc__pending_uploads.dynamic_transfer_buffer.mapped + nc__pending_uploads.dynamic_transfer_buffer.offset,
            surface->pixels,
            texture_size);
    nc__pending_uploads.dynamic_transfer_buffer.offset += texture_size;

    const uint32_t new_id = nc__sdl_gpu_texture_pool_append(&nc__ui_textures.textures, (nc__sdl_gpu_texture_t){
        .dimensions = { { (uint16_t)surface->w, (uint16_t)surface->h } },
    });

    SDL_DestroySurface(surface);

    nc__pending_texture_upload_vector_append(&nc__pending_uploads.vector, new_id);

    nc__string_uint_hashmap_set(&nc__ui_textures.name_to_id_map, strdup(name), new_id);
    assert(!nc__uint_string_hashmap_get(&nc__ui_textures.id_to_name_map, new_id));
    nc__uint_string_hashmap_set(&nc__ui_textures.id_to_name_map, new_id, strdup(name));

    return new_id;
#endif

    error:
    if (nc__pending_uploads.dynamic_transfer_buffer.offset == 0) {
        SDL_UnmapGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);
        nc__pending_uploads.dynamic_transfer_buffer.mapped = NULL;
    }
    SDL_ReleaseGPUTransferBuffer(device, new_transfer_buffer);
    SDL_DestroySurface(surface);
    return NC__INVALID_ID;
}

static bool nc__event_handler(void* user_data, SDL_Event* event) {
    (void)user_data;
    switch (event->type) {
        case SDL_EVENT_MOUSE_WHEEL:
            nc__scroll_delta.x += event->wheel.x;
            nc__scroll_delta.y += event->wheel.y;
            break;
    }

    return true;
}

static void nc__clay_error_handler(Clay_ErrorData error_data) {
    SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Clay error %d: %s\n",
            error_data.errorType,
            error_data.errorText.chars);
}

// When changing the measuring implementation, change it in nc__draw_debug_text, too!
// TODO: Font, letter spacing and line height
static Clay_Dimensions nc__measure_text(const Clay_StringSlice text, Clay_TextElementConfig* config, void* user_data) {
    (void)config;
    (void)user_data;

    const int16_t char_width = (int16_t)
            ((float)config->fontSize * NC__DEBUG_FONT_CHAR_WIDTH_SIZE_FACTOR * NC__FONT_SIZE_ADJUSTMENT_FACTOR);
    const int16_t char_height = (int16_t)((float)config->fontSize * NC__FONT_SIZE_ADJUSTMENT_FACTOR);

    int16_t width = 0, height = char_height;
    for (int i = 0; i < text.length; i++) {
        if (text.chars[i] == '\t') {
            // TODO: Measure tab char
        } else if (text.chars[i] == '\n') {
            height += char_height;
        } else {
            width += char_width;
        }
    }

    return (Clay_Dimensions){ (float)width, (float)height };
}

bool nc_ui_init(SDL_GPUDevice* device, const vkm_usvec2 viewport_size) {
    SDL_GPUShader* ui_vert = NULL, *ui_frag = NULL, *ui_image_vert = NULL, *ui_image_frag = NULL;

    nc__quads_to_draw.capacity = 8;
    nc__quads_to_draw.buffer = SDL_CreateGPUBuffer(
            device,
            &(SDL_GPUBufferCreateInfo){
                .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                .size = sizeof(nc__rectangle_t) * nc__quads_to_draw.capacity,
            });
    NC_CHECK_SDL_RESULT(nc__quads_to_draw.buffer);
    nc__quads_to_draw.transfer_buffer = SDL_CreateGPUTransferBuffer(
            device,
            &(SDL_GPUTransferBufferCreateInfo){
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = sizeof(nc__rectangle_t) * nc__quads_to_draw.capacity,
            });
    NC_CHECK_SDL_RESULT(nc__quads_to_draw.transfer_buffer);
    nc__quads_to_draw.mapped = SDL_MapGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer, true);
    NC_CHECK_SDL_RESULT(nc__quads_to_draw.mapped);

    nc__pending_uploads.dynamic_transfer_buffer.size = 1024 * 1024; // 1 MB, pretty arbitrary, enlarged as necessary
    nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer = SDL_CreateGPUTransferBuffer(
            device,
            &(SDL_GPUTransferBufferCreateInfo){
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = nc__pending_uploads.dynamic_transfer_buffer.size,
            });

    const unsigned font_texture_id = nc__load_texture(
            device,
            "__NC_INTERNAL_SPLEEN_FONT_DO_NOT_TOUCH",
            NC__TEXTURES_BASE_PATH "spleen" NC__TEXTURE_FILE_EXTENSION);
    if (font_texture_id == NC__INVALID_ID) {
        return false;
    }

    nc__pipeline = SDL_CreateGPUGraphicsPipeline(device, &(SDL_GPUGraphicsPipelineCreateInfo) {
        .vertex_shader = ui_vert,
        .fragment_shader = ui_frag,
        .vertex_input_state = {
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]) {
                {
                    .slot = 0,
                    .pitch = sizeof(pd_rectangle_t),
                    .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
                },
            },
            .num_vertex_buffers = 1,
            .vertex_attributes = (SDL_GPUVertexAttribute[]) {
                {
                    .location = 0,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4,
                    .offset = offsetof(pd_rectangle_t, rectangle),
                },
                {
                    .location = 1,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                    .offset = offsetof(pd_rectangle_t, color),
                },
                {
                    .location = 2,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                    .offset = offsetof(pd_rectangle_t, border_radiuses),
                },
                {
                    .location = 3,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4,
                    .offset = offsetof(pd_rectangle_t, border_thicknesses),
                },
                {
                    .location = 4,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2,
                    .offset = offsetof(pd_rectangle_t, character),
                },
                {
                    .location = 5,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2,
                    .offset = offsetof(pd_rectangle_t, draw_order),
                },
                {
                    .location = 6,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4,
                    .offset = offsetof(pd_rectangle_t, scissor),
                },
            },
            .num_vertex_attributes = 7,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_clip = true,
        },
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
            .enable_depth_test = true,
            .enable_depth_write = true,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]) {
                {
                    .format = SDL_GetGPUSwapchainTextureFormat(device, window),
                    .blend_state = {
                        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                        .color_blend_op = SDL_GPU_BLENDOP_ADD,
                        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
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

    const uint64_t size = Clay_MinMemorySize();
    // TODO: Leaks memory, maybe?
    const Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, malloc(size));

    Clay_Initialize(
            arena,
            (Clay_Dimensions){ (float)viewport_size.x, (float)viewport_size.y },
            (Clay_ErrorHandler){ .errorHandlerFunction = nc__clay_error_handler });
    Clay_SetMeasureTextFunction(nc__measure_text, NULL);

    SDL_AddEventWatch(nc__event_handler, NULL);

    return true;

    error:
    SDL_ReleaseGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);
    nc__pending_uploads = (nc__pending_uploads_t){ 0 };
    SDL_UnmapGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer);
    SDL_ReleaseGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer);
    SDL_ReleaseGPUBuffer(device, nc__quads_to_draw.buffer);
    nc__quads_to_draw = (nc__quads_to_draw_t){ 0 };
    return false;
}

static void nc__draw_rectangles(SDL_GPUDevice* device, const nc__rectangle_t* rectangles, const unsigned count) {
    SDL_GPUBuffer* new_buffer = NULL;
    SDL_GPUTransferBuffer* new_transfer_buffer = NULL;

    assert(nc__quads_to_draw.mapped);
    assert(nc__quads_to_draw.count <= nc__quads_to_draw.capacity);

    if (nc__quads_to_draw.count + count > nc__quads_to_draw.capacity) {
        // need more capacity: make new, larger buffers and copy existing data over
        const unsigned new_capacity = nc__next_power_of_2(nc__quads_to_draw.count + count);
        new_buffer = SDL_CreateGPUBuffer(
                device,
                &(SDL_GPUBufferCreateInfo){
                    .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                    .size = sizeof(nc__rectangle_t) * new_capacity,
                });
        NC_CHECK_SDL_RESULT(new_buffer);
        new_transfer_buffer = SDL_CreateGPUTransferBuffer(
                device,
                &(SDL_GPUTransferBufferCreateInfo){
                    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                    .size = sizeof(nc__rectangle_t) * new_capacity,
                });
        NC_CHECK_SDL_RESULT(new_transfer_buffer);
        nc__rectangle_t* new_mapped = SDL_MapGPUTransferBuffer(device, new_transfer_buffer, true);
        memcpy(new_mapped, nc__quads_to_draw.mapped, nc__quads_to_draw.count * sizeof(*nc__quads_to_draw.mapped));

        SDL_ReleaseGPUBuffer(device, nc__quads_to_draw.buffer);
        SDL_UnmapGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer);
        SDL_ReleaseGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer);

        nc__quads_to_draw.buffer = new_buffer;
        nc__quads_to_draw.transfer_buffer = new_transfer_buffer;
        nc__quads_to_draw.mapped = new_mapped;
        nc__quads_to_draw.capacity = new_capacity;
    }

    const bool is_text = rectangles[0].draw_order == NC__QUAD_IS_TEXT;
    for (unsigned i = 0; i < count; i++) {
        nc__quads_to_draw.mapped[nc__quads_to_draw.count + i] = rectangles[i];
        nc__quads_to_draw.mapped[nc__quads_to_draw.count + i].draw_order = nc__draw_order_counter;
        if (!is_text) {
            nc__draw_order_counter++;
        }

        const float max_radius = vkm_min(rectangles[i].rectangle.width, rectangles[i].rectangle.height) * 0.5f;
        nc__quads_to_draw.mapped[nc__quads_to_draw.count + i].border_radiuses.vec.x = vkm_min(rectangles[i].border_radiuses.vec.x, max_radius);
        nc__quads_to_draw.mapped[nc__quads_to_draw.count + i].border_radiuses.vec.y = vkm_min(rectangles[i].border_radiuses.vec.y, max_radius);
        nc__quads_to_draw.mapped[nc__quads_to_draw.count + i].border_radiuses.vec.z = vkm_min(rectangles[i].border_radiuses.vec.z, max_radius);
        nc__quads_to_draw.mapped[nc__quads_to_draw.count + i].border_radiuses.vec.w = vkm_min(rectangles[i].border_radiuses.vec.w, max_radius);
    }
    if (is_text) {
        nc__draw_order_counter++;
    }

    nc__quads_to_draw.count += count;

    return;

    error:
    SDL_ReleaseGPUTransferBuffer(device, new_transfer_buffer);
    SDL_ReleaseGPUBuffer(device, new_buffer);
    assert(false);
}

static void nc__draw_debug_text(
    SDL_GPUDevice* device,
    const vkm_svec2 position,
    const uint8_t font_size,
    const vkm_ubvec4 color,
    const char* text,
    unsigned length,
    const vkm_srect scissor
) {
    assert(font_size);

    if (!length) {
        length = (unsigned)strlen(text);
    }

    nc__rectangle_t* text_quads = malloc(length * sizeof(*text_quads));
    vkm_srect rectangle = {
        .x = position.x,
        .y = position.y,
        // When changing the sizing implementation, change it in nc__measure_text, too!
        .width = (int16_t)((float)font_size * NC__DEBUG_FONT_CHAR_WIDTH_SIZE_FACTOR * NC__FONT_SIZE_ADJUSTMENT_FACTOR),
        .height = (int16_t)((float)font_size * NC__FONT_SIZE_ADJUSTMENT_FACTOR),
    };
    unsigned quad_count = 0;
    for (unsigned character = 0; character < length; character++) {
        if (text[character] == '\r') {
            // Check if this is a W*ndows style new line character.
            // -1 to check whether this is the last character in the string. We don't want to read invalid memory!
            if (character < length - 1 && text[character + 1] == '\n') {
                continue;
            } else {
                // TODO: Render tab char
            }
        }
        if (text[character] == '\n') {
            rectangle.x = position.x;
            rectangle.y += rectangle.height;
        } else {
            text_quads[quad_count] = (nc__rectangle_t){
                .rectangle = rectangle,
                .color = color,
                .character = text[character],
                .scissor = scissor,
            };
            rectangle.x += rectangle.width;
            quad_count++;
        }
    }

    text_quads[0].draw_order = NC__QUAD_IS_TEXT;
    nc__draw_rectangles(device, text_quads, quad_count);
    free(text_quads);
}

static void nc__draw_ui_texture(
    const unsigned id,
    const vkm_srect rectangle,
    const vkm_ubvec4 color,
    const vkm_vec4* border_radiuses,
    const vkm_srect scissor
) {
    vkm_vec4 radiuses;
    if (border_radiuses) {
        const float max_radius = (float)vkm_min(rectangle.width, rectangle.height) * 0.5f;
        radiuses = (vkm_vec4) {
            .x = vkm_min(border_radiuses->x, max_radius),
            .y = vkm_min(border_radiuses->y, max_radius),
            .z = vkm_min(border_radiuses->z, max_radius),
            .w = vkm_min(border_radiuses->w, max_radius),
        };
    } else {
        radiuses = (vkm_vec4) { 0 };
    }
    nc__ui_images_t_append(&nc__ui_images, (nc__ui_image_t){
        .uniforms = {
            .rectangle = {
                .x = rectangle.x,
                .y = rectangle.y,
                .width = rectangle.width,
                .height = rectangle.height,
            },
            .color = {
                .r = (float)color.r / 255.0f,
                .g = (float)color.g / 255.0f,
                .b = (float)color.b / 255.0f,
                .a = (float)color.a / 255.0f,
            },
            .border_radiuses.vec = radiuses,
            .scissor = {
                .x = scissor.x,
                .y = scissor.y,
                .width = scissor.width,
                .height = scissor.height,
            },
            .draw_order = nc__draw_order_counter,
        },
        .id = id,
    });
    nc__draw_order_counter++;
}

void nc_render_clay_command_array(
    SDL_GPUDevice* device,
    const float delta_time,
    const vkm_usvec2 viewport_size,
    const Clay_RenderCommandArray* commands
) {
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)viewport_size.x, (float)viewport_size.y });
    Clay_Vector2 mouse_position;
    const SDL_MouseButtonFlags mouse_flags = SDL_GetMouseState(&mouse_position.x, &mouse_position.y);
    Clay_SetPointerState(mouse_position, (mouse_flags & SDL_BUTTON_LMASK) != 0);
    Clay_UpdateScrollContainers(true, nc__scroll_delta, delta_time);
    nc__scroll_delta = (Clay_Vector2){ 0 };

    vkm_srect scissor = { 0 };
    bool rendering_enabled = true;
    for (int i = 0; i < commands->length; i++) {
        Clay_RenderCommand *render_command = &commands->internalArray[i];

        if (!rendering_enabled && render_command->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
            continue;
        }

        nc__rectangle_t rectangle = {
            .rectangle = {
                .x = (int16_t)render_command->boundingBox.x,
                .y = (int16_t)render_command->boundingBox.y,
                .width = (int16_t)render_command->boundingBox.width,
                .height = (int16_t)render_command->boundingBox.height,
            },
            .scissor = scissor,
        };

        switch (render_command->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_NONE:
                break;
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
                if (!rectangle.rectangle.width || !rectangle.rectangle.height) {
                    // requested a rectangle with no area, do nothing
                    break;
                }
                rectangle.color = (vkm_ubvec4){ {
                    (uint8_t)render_command->renderData.rectangle.backgroundColor.r,
                    (uint8_t)render_command->renderData.rectangle.backgroundColor.g,
                    (uint8_t)render_command->renderData.rectangle.backgroundColor.b,
                    (uint8_t)render_command->renderData.rectangle.backgroundColor.a,
                } };
                rectangle.border_radiuses.vec = *(vkm_vec4*)&render_command->renderData.rectangle.cornerRadius;
                nc__draw_rectangles(device, &rectangle, 1);
                break;
            case CLAY_RENDER_COMMAND_TYPE_BORDER:
                if (
                    !render_command->renderData.border.width.left
                    && !render_command->renderData.border.width.right
                    && !render_command->renderData.border.width.top
                    && !render_command->renderData.border.width.bottom
                ) {
                    // requested a 0-width border, do nothing
                    break;
                }
                rectangle.color = (vkm_ubvec4){ {
                    (uint8_t)render_command->renderData.border.color.r,
                    (uint8_t)render_command->renderData.border.color.g,
                    (uint8_t)render_command->renderData.border.color.b,
                    (uint8_t)render_command->renderData.border.color.a,
                } };
                rectangle.border_radiuses.vec = *(vkm_vec4*)&render_command->renderData.border.cornerRadius;
                rectangle.border_thicknesses.vec = *(vkm_usvec4*)&render_command->renderData.border.width;
                nc__draw_rectangles(device, &rectangle, 1);
                break;
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
                assert(render_command->renderData.text.fontSize < 256);
                // TODO: Font, letter spacing and line height
                nc__draw_debug_text(
                        device,
                        *(vkm_svec2*)&rectangle.rectangle,
                        (uint8_t)render_command->renderData.text.fontSize,
                        (vkm_ubvec4){ {
                            (uint8_t)render_command->renderData.text.textColor.r,
                            (uint8_t)render_command->renderData.text.textColor.g,
                            (uint8_t)render_command->renderData.text.textColor.b,
                            (uint8_t)render_command->renderData.text.textColor.a,
                        } },
                        render_command->renderData.text.stringContents.chars,
                        render_command->renderData.text.stringContents.length,
                        scissor);
                break;
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
                if (!rectangle.rectangle.width || !rectangle.rectangle.height) {
                    // requested an image with no area, do nothing
                    break;
                }
                nc__draw_ui_texture(
                    (unsigned)(uintptr_t)render_command->renderData.image.imageData,
                    rectangle.rectangle,
                    (vkm_ubvec4){ {
                        (uint8_t)render_command->renderData.image.backgroundColor.r,
                        (uint8_t)render_command->renderData.image.backgroundColor.g,
                        (uint8_t)render_command->renderData.image.backgroundColor.b,
                        (uint8_t)render_command->renderData.image.backgroundColor.a,
                    } },
                    (vkm_vec4*)&render_command->renderData.border.cornerRadius,
                    scissor);
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                if (!rectangle.rectangle.width || !rectangle.rectangle.height) {
                    // requested a clip area of zero
                    rendering_enabled = false;
                } else {
                    scissor = (vkm_srect){
                        .x = rectangle.rectangle.x,
                        .y = rectangle.rectangle.y,
                        .width = rectangle.rectangle.width,
                        .height = rectangle.rectangle.height,
                    };
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                scissor = (vkm_srect){ 0 };
                rendering_enabled = true;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            default:
                assert(false);
                break;
        }
    }
}

void nc_upload_pending_ui_textures(SDL_GPUDevice* device, SDL_GPUCopyPass* copy_pass) {
    if (!nc__pending_uploads.vector.count) {
        return;
    }

    if (nc__pending_uploads.dynamic_transfer_buffer.mapped) {
        SDL_UnmapGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);
        nc__pending_uploads.dynamic_transfer_buffer.mapped = NULL;
    }

    for (unsigned i = 0, offset = 0; i < nc__pending_uploads.vector.count; i++) {
        // upload pending UI textures
        const uint32_t pending_upload_id = nc__pending_uploads.vector.array[i];
        assert(pending_upload_id < nc__ui_textures.textures.count);

        nc__sdl_gpu_texture_t* texture = &nc__ui_textures.textures.array[pending_upload_id];

        // UI texture at pending upload index is already uploaded
        assert(!texture->texture);

        texture->texture = SDL_CreateGPUTexture(
                device,
                &(SDL_GPUTextureCreateInfo) {
                    .type = SDL_GPU_TEXTURETYPE_2D,
                    .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                    .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                    .width = texture->dimensions.x,
                    .height = texture->dimensions.y,
                    .layer_count_or_depth = 1,
                    .num_levels = 1,
                });
        NC_CHECK_SDL_RESULT(texture->texture);
        SDL_UploadToGPUTexture(
                copy_pass,
                &(SDL_GPUTextureTransferInfo) {
                    .transfer_buffer = nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer,
                    .offset = offset,
                },
                &(SDL_GPUTextureRegion) {
                    .texture = texture->texture,
                    .w = texture->dimensions.x,
                    .h = texture->dimensions.y,
                    .d = 1,
                },
                false);

        // TODO: ASTC
        offset += texture->dimensions.x * texture->dimensions.y * 4;
    }
    nc__pending_texture_upload_vector_clear(&nc__pending_uploads.vector);
    nc__pending_uploads.dynamic_transfer_buffer.offset = 0;

    return;

    error:
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload new UI textures.");
    assert(false);
}

bool nc_draw_2d(
    SDL_GPUDevice* device,
    SDL_GPUCommandBuffer* command_buffer,
    SDL_GPUTexture* color_texture,
    SDL_GPUTexture* depth_texture,
    SDL_GPUSampler* sampler
) {
    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
            command_buffer,
            &(SDL_GPUColorTargetInfo) {
                .texture = color_texture,
                .load_op = SDL_GPU_LOADOP_LOAD,
                .store_op = SDL_GPU_STOREOP_STORE,
                .cycle = false,
            },
            1,
            &(SDL_GPUDepthStencilTargetInfo) {
                .texture = depth_texture,
                .clear_depth = 1.0f,
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_DONT_CARE,
                // Could be cycled because we don't care about previous values,
                // but since the depth texture is used together with the color texture
                // and we're loading the contents of the latter anyway, this actually doesn't matter.
                .cycle = false,
            });

    if (nc__quads_to_draw.count) {
        SDL_BindGPUGraphicsPipeline(render_pass, ui_pipeline);
        SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding) {
            .buffer = nc__quads_to_draw.buffer,
            .offset = 0,
        }, 1);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &ui_uniforms, sizeof(ui_uniforms));
        SDL_BindGPUFragmentSamplers(render_pass, 0, (SDL_GPUTextureSamplerBinding[]) {
            {
                // the font texture is always the first one
                // TODO: Do NOT allow deleting the font texture!
                .texture = nc__ui_textures.textures.array[0].texture,
                .sampler = sampler,
            },
        }, 1);
        SDL_DrawGPUPrimitives(render_pass, 4, nc__quads_to_draw.count, 0, 0);
    }

    if (nc__ui_images.count) {
        SDL_BindGPUGraphicsPipeline(render_pass, ui_image_pipeline);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &ui_uniforms, sizeof(ui_uniforms));
        for (unsigned i = 0; i < ui_images.count; i++) {
            if (!pd_is_ui_texture_id_valid(ui_images.ids[i])) {
                // TODO: Log warning, but don't spam it!
                continue;
            }

            const unsigned dense_index = ui_textures.dense[ui_images.ids[i]];
            PD_ASSERT(ui_textures.textures[dense_index], "Tried to draw an UI texture that is pending upload.");
            SDL_PushGPUVertexUniformData(command_buffer, 1, &ui_images.array[i], sizeof(*ui_images.array));
            SDL_BindGPUFragmentSamplers(render_pass, 0, (SDL_GPUTextureSamplerBinding[]) {
                {
                    .texture = ui_textures.textures[dense_index],
                    .sampler = linear_samplers[0],
                },
            }, 1);
            SDL_DrawGPUPrimitives(render_pass, 4, 1, 0, 0);
        }
    }

    SDL_EndGPURenderPass(render_pass);
}

void nc_ui_fini(SDL_GPUDevice* device) {
    nc__string_uint_hashmap_fini(&nc__ui_textures.name_to_id_map);
    nc__uint_string_hashmap_fini(&nc__ui_textures.id_to_name_map);
    for (uint32_t i = 0; i < nc__ui_textures.textures.count; i++) {
        SDL_ReleaseGPUTexture(device, nc__ui_textures.textures.array[i].texture);
    }
    nc__sdl_gpu_texture_pool_fini(&nc__ui_textures.textures);
    nc__ui_images_t_fini(&nc__ui_images);
    SDL_UnmapGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);
    SDL_ReleaseGPUTransferBuffer(device, nc__pending_uploads.dynamic_transfer_buffer.transfer_buffer);
    nc__pending_texture_upload_vector_fini(&nc__pending_uploads.vector);
    nc__pending_uploads = (nc__pending_uploads_t){ 0 };
    SDL_UnmapGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer);
    SDL_ReleaseGPUTransferBuffer(device, nc__quads_to_draw.transfer_buffer);
    SDL_ReleaseGPUBuffer(device, nc__quads_to_draw.buffer);
    nc__quads_to_draw = (nc__quads_to_draw_t){ 0 };
}
