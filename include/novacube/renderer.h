#pragma once
#ifndef NOVACUBE_RENDERER_H_
#define NOVACUBE_RENDERER_H_

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>

typedef struct nc_renderer_texture_t nc_renderer_texture_t;
typedef struct nc_renderer_buffer_t nc_renderer_buffer_t;

typedef struct nc_renderer_t nc_renderer_t;

typedef struct nc_renderer_create_info_t {
    const char* window_title;
    uint16_t window_width;
    uint16_t window_height;
    bool fullscreen;
} nc_renderer_create_info_t;

typedef enum nc_renderer_buffer_kind_t {
    NC_RENDERER_BUFFER_KIND_TERRAIN_INSTANCES = 1,
    NC_RENDERER_BUFFER_KIND_GUI_VERTICES,
    NC_RENDERER_BUFFER_KIND_GUI_INDICES,

    NC_RENDERER_BUFFER_KIND_MAX = NC_RENDERER_BUFFER_KIND_GUI_INDICES,
} nc_renderer_buffer_kind_t;

typedef struct nc_renderer_overlay_draw_command_t {
    const nc_renderer_texture_t* texture;
    SDL_Rect clip_rect;
    uint32_t element_count;
    uint32_t first_index;
} nc_renderer_overlay_draw_command_t;

typedef struct nc_renderer_opaque_draw_t {
    const nc_renderer_buffer_t* instance_buffer;
    uint32_t instance_count;
    const nc_renderer_texture_t* texture;
    const vkm_mat4* view_projection;
} nc_renderer_opaque_draw_t;

typedef struct nc_renderer_overlay_draw_t {
    const nc_renderer_buffer_t* vertex_buffer;
    const nc_renderer_buffer_t* index_buffer;
    const nc_renderer_overlay_draw_command_t* draw_commands;
    uint32_t draw_command_count;
} nc_renderer_overlay_draw_t;

typedef struct nc_renderer_frame_t {
    const nc_renderer_opaque_draw_t* opaque_draws;
    uint32_t opaque_draw_count;
    const nc_renderer_overlay_draw_t* overlay_draws;
    uint32_t overlay_draw_count;
} nc_renderer_frame_t;

nc_renderer_t* nc_renderer_init(const nc_renderer_create_info_t* info);
bool nc_renderer_handle_event(nc_renderer_t* renderer, const SDL_Event* event);
bool nc_renderer_begin_frame(nc_renderer_t* renderer);
bool nc_renderer_end_frame(nc_renderer_t* renderer);
bool nc_renderer_set_relative_mouse_mode(nc_renderer_t* renderer, bool enabled);
bool nc_renderer_is_foreground(const nc_renderer_t* renderer);
vkm_usvec2 nc_renderer_get_window_size(const nc_renderer_t* renderer);
vkm_usvec2 nc_renderer_get_viewport(const nc_renderer_t* renderer);
float nc_renderer_get_window_display_scale(const nc_renderer_t* renderer);
nc_renderer_buffer_t* nc_renderer_create_buffer(
    nc_renderer_t* renderer,
    nc_renderer_buffer_kind_t kind,
    uint32_t initial_capacity);
void nc_renderer_destroy_buffer(nc_renderer_t* renderer, nc_renderer_buffer_t* buffer);
bool nc_renderer_queue_buffer_upload(
    nc_renderer_t* renderer,
    nc_renderer_buffer_t* buffer,
    const void* data,
    uint32_t size);
nc_renderer_texture_t* nc_renderer_create_rgba_texture_2d(
    nc_renderer_t* renderer,
    int16_t width,
    int16_t height,
    const void* pixels);
nc_renderer_texture_t* nc_renderer_create_texture_2d_from_file(
    nc_renderer_t* renderer,
    const char* path);
nc_renderer_texture_t* nc_renderer_create_texture_array_from_files(
    nc_renderer_t* renderer,
    const char* const* paths,
    uint16_t path_count);
void nc_renderer_destroy_texture(nc_renderer_t* renderer, nc_renderer_texture_t* texture);
bool nc_renderer_draw(nc_renderer_t* renderer, const nc_renderer_frame_t* frame);
float nc_renderer_get_window_pixel_density(const nc_renderer_t* renderer);
void nc_renderer_fini(nc_renderer_t* renderer);
#endif
