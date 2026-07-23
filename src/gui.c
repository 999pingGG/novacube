#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <novacube/cvar.h>
#include <novacube/cvkm.h>
#include <novacube/error_handling.h>
#include <novacube/gui.h>
#include <novacube/player_input.h>
#include <novacube/renderer.h>
#include <novacube/string_builder.h>

#include "nuklear_backend.h"

#ifdef ANDROID
#define NC__GUI_ASSETS_BASE_PATH ""
#define NC__GUI_TEXTURE_EXTENSION ".astc"
#else
#define NC__GUI_ASSETS_BASE_PATH "assets/"
#define NC__GUI_TEXTURE_EXTENSION ".png"
#endif

#define NC__GUI_MIN_SCALE 0.5f
#define NC__GUI_MAX_SCALE 3.0f
#define NC__GUI_MAX_LOGICAL_SIZE 32767.0f

typedef struct nc__gui_texture_t {
    nc_renderer_texture_t* texture;
    struct nk_image nuklear_image;
} nc__gui_texture_t;

typedef struct nc_gui_context_t {
    vkm_usvec2 window_size;
    vkm_usvec2 pixel_viewport;
    float scale;

    nc_nuklear_backend_t* nuklear;
    nc__gui_texture_t control_textures[NC_PLAYER_INPUT_CONTROL_COUNT];
    nc_string_builder_t debug_string_builder;
    struct nk_rect safe_area;

    // TODO: Delete those.
    int demo_difficulty;
    float demo_volume;
    bool demo_open;
} nc_gui_context_t;

static const char* nc__gui_control_texture_paths[NC_PLAYER_INPUT_CONTROL_COUNT] = {
    NC__GUI_ASSETS_BASE_PATH "textures/gui/left"        NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/right"       NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/up"          NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/down"        NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/up"          NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/down"        NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/place"       NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/remove"      NC__GUI_TEXTURE_EXTENSION,
};

static float nc__gui_sanitized_user_scale(void) {
    const double configured_scale = nc_cvar_get_gui_scale();
    if (!isfinite(configured_scale)) {
        return 1.0f;
    }

    return vkm_clamp((float)configured_scale, NC__GUI_MIN_SCALE, NC__GUI_MAX_SCALE);
}

static float nc__gui_effective_scale(const float window_display_scale) {
    const float automatic_scale = isfinite(window_display_scale) && window_display_scale > 0.0f
            ? window_display_scale
            : 1.0f;
    return automatic_scale * nc__gui_sanitized_user_scale();
}

static float nc__gui_nonnegative_size(const double value) {
    if (!isfinite(value) || value <= 0.0) {
        return 0.0f;
    }

    return (float)vkm_min(value, (double)NC__GUI_MAX_LOGICAL_SIZE);
}

static nc_nuklear_view_t nc__gui_get_nuklear_view(const nc_gui_context_t* context) {
    return (nc_nuklear_view_t){
        .window_size = context->window_size,
        .pixel_viewport = context->pixel_viewport,
        .scale = context->scale,
    };
}

static vkm_vec2 nc__gui_window_to_gui_position(
    const nc_gui_context_t* context,
    const float window_x,
    const float window_y
) {
    if (context->window_size.x == 0 || context->window_size.y == 0 || context->scale <= 0.0f) {
        return (vkm_vec2){ { 0.0f, 0.0f } };
    }

    return (vkm_vec2){ {
        window_x * (float)context->pixel_viewport.x / (float)context->window_size.x / context->scale,
        window_y * (float)context->pixel_viewport.y / (float)context->window_size.y / context->scale,
    } };
}

static vkm_vec2 nc__gui_to_pixel_position(
    const nc_gui_context_t* context,
    const vkm_vec2 position
) {
    return (vkm_vec2){ { position.x * context->scale, position.y * context->scale } };
}

static void nc__gui_update_safe_area(nc_gui_context_t* context, const nc_renderer_t* renderer) {
    SDL_Rect rect;
    nc_renderer_get_window_safe_area(renderer, &rect);
    const vkm_vec2 top_left = nc__gui_window_to_gui_position(context, (float)rect.x, (float)rect.y);
    const vkm_vec2 bottom_right = nc__gui_window_to_gui_position(
            context,
            (float)(rect.x + rect.w),
            (float)(rect.y + rect.h));
    context->safe_area = nk_rect(
            top_left.x,
            top_left.y,
            vkm_max(bottom_right.x - top_left.x, 0.0f),
            vkm_max(bottom_right.y - top_left.y, 0.0f));
}

static void nc__gui_destroy_texture(nc_renderer_t* renderer, nc__gui_texture_t* texture) {
    if (texture->texture) {
        nc_renderer_destroy_texture(renderer, texture->texture);
        *texture = (nc__gui_texture_t){ 0 };
    }
}

static bool nc__gui_load_texture(
    nc_renderer_t* renderer,
    const char* path,
    nc__gui_texture_t* texture
) {
    texture->texture = nc_renderer_create_texture_2d_from_file(
            renderer,
            NC_RENDERER_TEXTURE_TYPE_COLOR,
            path);
    if (!texture->texture) {
        return false;
    }

    texture->nuklear_image = nk_image_ptr(texture->texture);
    return true;
}

// This function issues one draw text command for every line contained in the debug text buffer.
static void nc__gui_draw_debug_text(
    const nc_gui_context_t* context,
    struct nk_command_buffer* canvas,
    struct nk_rect rect
) {
    const struct nk_user_font* font = nc_nuklear_backend_get_context(context->nuklear)->style.font;
    for (size_t current_beginning = 0, current_position = 0;
         context->debug_string_builder.data[current_position];) {
        while (    context->debug_string_builder.data[current_position]
                && context->debug_string_builder.data[current_position] != '\n') {
            current_position++;
        }

        const int length = (int)(current_position - current_beginning);
        nk_draw_text(
                canvas,
                rect,
                &context->debug_string_builder.data[current_beginning],
                length,
                font,
                nk_rgba(0, 0, 0, 255),
                nk_rgba(255, 255, 255, 255));
        rect.y += font->height;

        if (context->debug_string_builder.data[current_position] == '\n') {
            current_position++;
            current_beginning += (size_t)length + 1;
        }
    }
}

static void nc__gui_build_hud(
    nc_gui_context_t* context,
    const nc_player_input_overlay_t* player_input
) {
    struct nk_context* nuklear = nc_nuklear_backend_get_context(context->nuklear);
    const struct nk_style_window saved_window_style = nuklear->style.window;
    nuklear->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    nuklear->style.window.background = nk_rgba(0, 0, 0, 0);
    nuklear->style.window.border_color = nk_rgba(0, 0, 0, 0);
    nuklear->style.window.spacing = nk_vec2(0.0f, 0.0f);
    nuklear->style.window.padding = nk_vec2(0.0f, 0.0f);
    nuklear->style.window.group_padding = nk_vec2(0.0f, 0.0f);
    nuklear->style.window.border = 0.0f;

    if (nk_begin(
            nuklear,
            "hud-overlay",
            context->safe_area,
            NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT)) {
        struct nk_command_buffer* canvas = nk_window_get_canvas(nuklear);
        if (player_input->touch_controls_enabled) {
            for (uint8_t i = 0; i < (uint8_t)NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
                if (!player_input->controls_visible[i]) {
                    continue;
                }

                const SDL_FRect* input_rect = &player_input->control_rects[i];
                const struct nk_rect rect = nk_rect(
                        input_rect->x,
                        input_rect->y,
                        input_rect->w,
                        input_rect->h);
                const bool pressed = player_input->controls_pressed[i];
                const float inset = rect.w * 0.18f;
                const struct nk_rect image_rect = nk_rect(
                        rect.x + inset,
                        rect.y + inset,
                        rect.w - inset * 2.0f,
                        rect.h - inset * 2.0f);

                nk_fill_rect(
                        canvas,
                        rect,
                        rect.w * 0.22f,
                        pressed ? nk_rgba(255, 255, 255, 52) : nk_rgba(0, 0, 0, 96));
                nk_stroke_rect(
                        canvas,
                        rect,
                        rect.w * 0.22f,
                        2.0f,
                        pressed ? nk_rgba(255, 255, 255, 160) : nk_rgba(255, 255, 255, 64));
                nk_draw_image(
                        canvas,
                        image_rect,
                        &context->control_textures[i].nuklear_image,
                        pressed ? nk_rgba(255, 255, 255, 255) : nk_rgba(255, 255, 255, 224));
            }
        }

        if (context->debug_string_builder.length) {
            nc__gui_draw_debug_text(context, canvas, context->safe_area);
        }
    }
    nk_end(nuklear);
    nuklear->style.window = saved_window_style;
}

static void nc__gui_build_demo(nc_gui_context_t* context) {
    if (!context->demo_open) {
        return;
    }

    struct nk_context* nuklear = nc_nuklear_backend_get_context(context->nuklear);
    if (nk_begin(
            nuklear,
            "Show",
            nk_rect(50.0f, 50.0f, 220.0f, 220.0f),
            NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        nk_layout_row_static(nuklear, 30.0f, 80, 1);
        if (nk_button_label(nuklear, "button")) {
            // This is a backend smoke test; production actions will be connected by their owning screen.
        }

        nk_layout_row_dynamic(nuklear, 30.0f, 2);
        if (nk_option_label(nuklear, "easy", context->demo_difficulty == 0)) {
            context->demo_difficulty = 0;
        }
        if (nk_option_label(nuklear, "hard", context->demo_difficulty == 1)) {
            context->demo_difficulty = 1;
        }

        nk_layout_row_begin(nuklear, NK_DYNAMIC, 30.0f, 2);
        nk_layout_row_push(nuklear, 0.38f);
        nk_label(nuklear, "Volume:", NK_TEXT_LEFT);
        nk_layout_row_push(nuklear, 0.62f);
        nk_slider_float(nuklear, 0.0f, &context->demo_volume, 1.0f, 0.1f);
        nk_layout_row_end(nuklear);
    }
    nk_end(nuklear);
    if (nk_window_is_closed(nuklear, "Show")) {
        context->demo_open = false;
    }
}

nc_gui_context_t* nc_gui_init(nc_renderer_t* renderer) {
    nc_gui_context_t* context = calloc(1, sizeof(*context));
    context->window_size = nc_renderer_get_window_size(renderer);
    context->pixel_viewport = nc_renderer_get_viewport(renderer);
    context->scale = nc__gui_effective_scale(nc_renderer_get_window_display_scale(renderer));
    context->nuklear = nc_nuklear_backend_init(renderer, context->scale);
    if (!context->nuklear) {
        goto error;
    }

    for (int i = 0; i < NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        if (!nc__gui_load_texture(renderer, nc__gui_control_texture_paths[i], &context->control_textures[i])) {
            goto error;
        }
    }

    nc_string_builder_init(&context->debug_string_builder);
    nc__gui_update_safe_area(context, renderer);
    context->demo_volume = 0.6f;
    context->demo_open = true;
    return context;

error:
    nc_gui_fini(context, renderer);
    return NULL;
}

bool nc_gui_update_window_metrics(nc_gui_context_t* context, nc_renderer_t* renderer) {
    const vkm_usvec2 window_size = nc_renderer_get_window_size(renderer);
    const vkm_usvec2 pixel_viewport = nc_renderer_get_viewport(renderer);
    const float scale = nc__gui_effective_scale(nc_renderer_get_window_display_scale(renderer));
    if (       context->window_size.x == window_size.x
            && context->window_size.y == window_size.y
            && context->pixel_viewport.x == pixel_viewport.x
            && context->pixel_viewport.y == pixel_viewport.y
            && context->scale == scale) {
        return true;
    }

    context->window_size = window_size;
    context->pixel_viewport = pixel_viewport;
    context->scale = scale;
    nc__gui_update_safe_area(context, renderer);
    return nc_nuklear_backend_set_scale(context->nuklear, renderer, scale);
}

float nc_gui_get_scale(const nc_gui_context_t* context) {
    return context->scale;
}

bool nc_gui_handle_event(
    nc_gui_context_t* context,
    const SDL_Event* event,
    const bool mouse_input_enabled
) {
    const nc_nuklear_view_t view = nc__gui_get_nuklear_view(context);
    return nc_nuklear_backend_handle_event(
            context->nuklear,
            &view,
            event,
            mouse_input_enabled);
}

bool nc_gui_is_keyboard_captured(const nc_gui_context_t* context) {
    return nc_nuklear_backend_is_keyboard_captured(context->nuklear);
}

bool nc_gui_prepare_frame(
    nc_gui_context_t* context,
    nc_renderer_t* renderer,
    const nc_player_input_overlay_t* player_input,
    const float delta_time
) {
    nc__gui_update_safe_area(context, renderer);
    nc_nuklear_backend_begin_frame(context->nuklear, delta_time);
    nc__gui_build_hud(context, player_input);
    nc__gui_build_demo(context);

    const nc_nuklear_view_t view = nc__gui_get_nuklear_view(context);
    const bool result = nc_nuklear_backend_end_frame(context->nuklear, renderer, &view);
    nc_string_builder_clear(&context->debug_string_builder);
    return result;
}

void nc_gui_get_overlay_draw(const nc_gui_context_t* context, nc_renderer_overlay_draw_t* draw) {
    const nc_nuklear_view_t view = nc__gui_get_nuklear_view(context);
    nc_nuklear_backend_get_draw(context->nuklear, &view, draw);
}

void nc_gui_get_procedural_overlay_draw(
    const nc_gui_context_t* context,
    const nc_player_input_overlay_t* player_input,
    nc_renderer_procedural_overlay_draw_t* draw
) {
    const float crosshair_size = vkm_min(
            nc__gui_nonnegative_size(nc_cvar_get_crosshair_size()) * context->scale,
            (float)vkm_min(context->pixel_viewport.x, context->pixel_viewport.y));
    *draw = (nc_renderer_procedural_overlay_draw_t){
        .analog_stick_ring_radius = player_input->analog_stick_ring_radius * context->scale,
        .analog_stick_ring_thickness = player_input->analog_stick_ring_thickness * context->scale,
        .analog_stick_radius = player_input->analog_stick_radius * context->scale,
        .crosshair_size = crosshair_size,
    };

    for (size_t i = 0; i < 2; i++) {
        draw->analog_sticks_active[i] = player_input->analog_sticks_active[i];
        draw->analog_stick_ring_positions[i] = nc__gui_to_pixel_position(
                context,
                player_input->analog_stick_ring_positions[i]);
        draw->analog_stick_positions[i] = nc__gui_to_pixel_position(
                context,
                player_input->analog_stick_positions[i]);
    }
}

void nc_gui_append_debug_text(nc_gui_context_t* context, const char* text, const size_t length) {
    nc_string_builder_append(&context->debug_string_builder, text, length ? length : strlen(text));
}

void nc_gui_fini(nc_gui_context_t* context, nc_renderer_t* renderer) {
    if (!context) {
        return;
    }

    nc_string_builder_fini(&context->debug_string_builder);
    for (int i = 0; i < NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        nc__gui_destroy_texture(renderer, &context->control_textures[i]);
    }
    nc_nuklear_backend_fini(context->nuklear, renderer);
    free(context);
}
