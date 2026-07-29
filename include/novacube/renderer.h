#pragma once
#ifndef NOVACUBE_RENDERER_H_
#define NOVACUBE_RENDERER_H_

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include <novacube/cvar.h>
#include <novacube/cvkm.h>

typedef struct nc_renderer_texture_t nc_renderer_texture_t;
typedef struct nc_renderer_buffer_t nc_renderer_buffer_t;

typedef struct nc_renderer_t nc_renderer_t;

typedef struct nc_renderer_create_info_t {
    const char* window_title;
    uint16_t window_width;
    uint16_t window_height;
    uint16_t refresh_rate;
    nc_video_mode_t video_mode;
    bool prefer_low_power;
} nc_renderer_create_info_t;

typedef enum nc_renderer_buffer_usage_t {
    NC_RENDERER_BUFFER_USAGE_VERTEX = 1,
    NC_RENDERER_BUFFER_USAGE_INDEX,
    NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ,

    NC_RENDERER_BUFFER_USAGE_COUNT = NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ,
} nc_renderer_buffer_usage_t;

typedef enum nc_renderer_texture_type_t {
    // Human-viewable color stored with sRGB encoding. Sampling returns linear RGB; alpha remains linear.
    NC_RENDERER_TEXTURE_TYPE_COLOR = 1,
    // Non-color values such as masks, coverage, normals or lookup data. Sampling returns the stored values unchanged.
    NC_RENDERER_TEXTURE_TYPE_DATA,
} nc_renderer_texture_type_t;

// Tags the union in nc_renderer_overlay_draw_command_t. Rectangle commands reference a batch;
// image commands carry one texture because changing textures breaks a rectangle batch.
typedef uint8_t nc_renderer_overlay_command_type_t;
enum {
    NC_RENDERER_OVERLAY_COMMAND_RECTANGLES = 1,
    NC_RENDERER_OVERLAY_COMMAND_IMAGE,
};

typedef struct nc_renderer_overlay_rectangle_t {
    SDL_FRect rectangle;
    vkm_vec4 color;
    vkm_vec4 corner_radii;
    vkm_uvec4 border_widths;
    vkm_vec4 overlay_color;
    uint32_t character;
} nc_renderer_overlay_rectangle_t;

typedef struct nc_renderer_overlay_image_t {
    const nc_renderer_texture_t* texture;
    SDL_FRect rectangle;
    vkm_vec4 color;
    vkm_vec4 corner_radii;
    vkm_vec4 overlay_color;
} nc_renderer_overlay_image_t;

// One ordered rendering operation. Keeping order here is required for correct alpha blending.
typedef struct nc_renderer_overlay_draw_command_t {
    nc_renderer_overlay_command_type_t type;
    bool clip_enabled;
    SDL_FRect clip_rect;
    union {
        struct {
            uint32_t first_rectangle;
            uint32_t rectangle_count;
        } rectangles;
        nc_renderer_overlay_image_t image;
    };
} nc_renderer_overlay_draw_command_t;

typedef struct nc_renderer_chunk_draw_t {
    // SSBO containing an array of nc_mesh_quad_t.
    nc_renderer_buffer_t* chunk_buffer;
    // SSBO containing an array of nc_mesh_face_data_t.
    nc_renderer_buffer_t* face_data_buffer;
    const nc_renderer_texture_t* texture;
    vkm_vec3 position;
    uint32_t quad_count;
} nc_renderer_chunk_draw_t;

#define TDS_DECLARE
#define TDS_VALUE_T nc_renderer_chunk_draw_t
#define TDS_TYPE nc_renderer_chunk_draw_vec
#include <tds/vector.h>

// Rectangle data is stored separately so adjacent solid/text commands can be drawn as one
// instanced batch.
typedef struct nc_renderer_overlay_draw_t {
    // TODO: Can those be replaced with a TDS vector?
    const nc_renderer_overlay_rectangle_t* rectangles;
    uint32_t rectangle_count;
    const nc_renderer_overlay_draw_command_t* draw_commands;
    uint32_t draw_command_count;
    const nc_renderer_texture_t* font_texture;
} nc_renderer_overlay_draw_t;

typedef struct nc_renderer_procedural_overlay_draw_t {
    vkm_vec2 analog_stick_ring_positions[2];
    vkm_vec2 analog_stick_positions[2];
    bool analog_sticks_active[2];
    float analog_stick_ring_radius;
    float analog_stick_ring_thickness;
    float analog_stick_radius;
    float crosshair_size;
} nc_renderer_procedural_overlay_draw_t;

typedef struct nc_renderer_block_highlight_draw_t {
    vkm_vec3 position;
    vkm_vec3 normal;
    float time;
    bool shown;
} nc_renderer_block_highlight_draw_t;

#define NC_RENDERER_SKY_GRADIENT_COLOR_COUNT 4

typedef struct nc_renderer_sky_draw_t {
    // Linear RGB colors ordered from the lowest to the highest elevation.
    vkm_vec4 gradient_colors[NC_RENDERER_SKY_GRADIENT_COLOR_COUNT];
    // Strictly increasing direction.y values in the [-1, 1] range.
    vkm_vec4 gradient_stops;
} nc_renderer_sky_draw_t;

typedef struct nc_renderer_frame_t {
    const vkm_mat4* view_projection;
    vkm_vec3 camera_position;
    // Multiplier for sky light, in the [0, 1] range.
    float sunlight_intensity;
    const nc_renderer_sky_draw_t* sky_draw;
    const nc_renderer_chunk_draw_vec* opaque_chunk_draws;
    const nc_renderer_chunk_draw_vec* transparent_chunk_draws;
    const nc_renderer_overlay_draw_t* overlay_draws;
    uint32_t overlay_draw_count;
    const nc_renderer_procedural_overlay_draw_t* procedural_overlay_draw;
    const nc_renderer_block_highlight_draw_t* block_highlight_draw;
} nc_renderer_frame_t;

nc_renderer_t* nc_renderer_init(const nc_renderer_create_info_t* info);
bool nc_renderer_handle_event(nc_renderer_t* renderer, const SDL_Event* event);
bool nc_renderer_begin_frame(nc_renderer_t* renderer);
bool nc_renderer_end_frame(nc_renderer_t* renderer);
bool nc_renderer_set_relative_mouse_mode(nc_renderer_t* renderer, bool enabled);
bool nc_renderer_is_relative_mouse_mode(const nc_renderer_t* renderer);
bool nc_renderer_is_foreground(const nc_renderer_t* renderer);
vkm_usvec2 nc_renderer_get_window_size(const nc_renderer_t* renderer);
// Returns actual framebuffer pixels in the current display orientation, not SDL window units.
vkm_usvec2 nc_renderer_get_framebuffer_size(const nc_renderer_t* renderer);
void nc_renderer_get_window_safe_area(const nc_renderer_t* renderer, SDL_Rect* rect);
nc_renderer_buffer_t* nc_renderer_create_buffer(
        nc_renderer_t* renderer,
        nc_renderer_buffer_usage_t usage,
        uint32_t initial_size);
void nc_renderer_destroy_buffer(nc_renderer_t* renderer, nc_renderer_buffer_t* buffer);
bool nc_renderer_queue_buffer_upload(
        nc_renderer_t* renderer,
        nc_renderer_buffer_t* buffer,
        const void* data,
        uint32_t size);
nc_renderer_texture_t* nc_renderer_create_rgba_texture_2d(
        nc_renderer_t* renderer,
        nc_renderer_texture_type_t type,
        int16_t width,
        int16_t height,
        const void* pixels);
nc_renderer_texture_t* nc_renderer_create_texture_2d_from_file(
        nc_renderer_t* renderer,
        nc_renderer_texture_type_t type,
        const char* path);
nc_renderer_texture_t* nc_renderer_create_texture_array_from_files(
        nc_renderer_t* renderer,
        nc_renderer_texture_type_t type,
        const char* const* paths,
        uint16_t path_count);
void nc_renderer_destroy_texture(nc_renderer_t* renderer, nc_renderer_texture_t* texture);
bool nc_renderer_draw(nc_renderer_t* renderer, const nc_renderer_frame_t* frame);
void nc_renderer_fini(nc_renderer_t* renderer);

#endif
