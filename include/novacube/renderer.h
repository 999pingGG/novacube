#pragma once
#ifndef NOVACUBE_RENDERER_H_
#define NOVACUBE_RENDERER_H_

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include <novacube/asset_manager.h>
#include <novacube/cvar.h>
#include <novacube/cvkm.h>

typedef struct nc_renderer_texture_t nc_renderer_texture_t;
// High-level renderer-owned chunk geometry. Its paging allocation and Vulkan address stay private.
typedef struct nc_renderer_chunk_mesh_t nc_renderer_chunk_mesh_t;

typedef struct nc_renderer_t nc_renderer_t;

typedef struct nc_renderer_create_info_t {
    const char* window_title;
    uint16_t window_width;
    uint16_t window_height;
    uint16_t refresh_rate;
    nc_video_mode_t video_mode;
    bool prefer_low_power;
} nc_renderer_create_info_t;

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
    // The frame list selects the opaque or transparent pass; the mesh stores that pass's packed offset and count.
    const nc_renderer_chunk_mesh_t* mesh;
    vkm_vec3 position;
} nc_renderer_chunk_draw_t;

// CPU data for one opaque or transparent chunk pass. The renderer packs both passes into one device allocation;
// callers retain ownership of the arrays and may release them as soon as nc_renderer_upload_chunk_mesh returns.
typedef struct nc_renderer_chunk_mesh_data_t {
    const void* quads;
    const void* face_data;
    uint32_t quad_count;
    uint32_t face_data_count;
} nc_renderer_chunk_mesh_data_t;

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
    const nc_renderer_texture_t* terrain_texture_array;
    const nc_renderer_overlay_draw_t* overlay_draws;
    uint32_t overlay_draw_count;
    const nc_renderer_procedural_overlay_draw_t* procedural_overlay_draw;
    const nc_renderer_block_highlight_draw_t* block_highlight_draw;
} nc_renderer_frame_t;

nc_renderer_t* nc_renderer_init(const nc_renderer_create_info_t* info, nc_asset_manager_t* asset_manager);
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
// Treat *mesh as an optional renderer-owned resource: initialize it to NULL and keep passing the same slot on remesh.
// Shrinks retain capacity, growth replaces it, and empty opaque and transparent data releases it.
bool nc_renderer_upload_chunk_mesh(
        nc_renderer_t* renderer,
        nc_renderer_chunk_mesh_t** mesh,
        const nc_renderer_chunk_mesh_data_t* opaque,
        const nc_renderer_chunk_mesh_data_t* transparent);
void nc_renderer_destroy_chunk_mesh(nc_renderer_t* renderer, nc_renderer_chunk_mesh_t* mesh);
// The texture creation functions below copy pixels into renderer-owned upload storage before returning; callers retain
// ownership of every input buffer.
nc_renderer_texture_t* nc_renderer_create_rgba_texture_2d(
        nc_renderer_t* renderer,
        bool is_color_data,
        int16_t width,
        int16_t height,
        const void* pixels);
nc_renderer_texture_t* nc_renderer_create_texture_from_baked_assets(
        nc_renderer_t* renderer,
        const nc_texture_baked_asset_t* assets,
        uint16_t asset_count,
        nc_texture_type_t texture_type,
        bool is_color_data);
void nc_renderer_destroy_texture(nc_renderer_t* renderer, nc_renderer_texture_t* texture);
bool nc_renderer_draw(nc_renderer_t* renderer, const nc_renderer_frame_t* frame);
void nc_renderer_fini(nc_renderer_t* renderer);

#endif
