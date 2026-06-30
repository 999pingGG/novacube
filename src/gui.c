#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <novacube/configuration.h>
#include <novacube/cvkm.h>
#include <novacube/gui.h>
#include <novacube/error_handling.h>
#include <novacube/macros.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>

#define NK_PRIVATE
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_BUTTON_TRIGGER_ON_RELEASE
#define NK_KEYSTATE_BASED_INPUT
#define NK_ASSERT NC_ASSERT

#define NK_IMPLEMENTATION
NC_IGNORE_ALL_WARNINGS_START
#include <nuklear.h>
NC_IGNORE_ALL_WARNINGS_END

#ifdef ANDROID
#define NC__GUI_ASSETS_BASE_PATH ""
#define NC__GUI_TEXTURE_EXTENSION ".astc"
#else
#define NC__GUI_ASSETS_BASE_PATH "assets/"
#define NC__GUI_TEXTURE_EXTENSION ".png"
#endif

#define NC__GUI_INITIAL_BUFFER_CAPACITY 65536
#define NC__GUI_MAX_TOUCHES 8
#define NC__TOUCH_CONTROLS_SWITCH_THRESHOLD 0.2f
#define NC__GUI_BUTTON_SIZE_AT_SCALE_1 100.0f
#define NC__GUI_CROSSHAIR_SIZE_AT_SCALE_1 28.0f
#define NC__GUI_ANALOG_STICK_RING_RADIUS_AT_SCALE_1 64.0f
#define NC__GUI_ANALOG_STICK_RING_THICKNESS_AT_SCALE_1 4.0f
#define NC__GUI_ANALOG_STICK_RADIUS_AT_SCALE_1 28.0f

typedef uint8_t nc__gui_control_id_t;
enum {
    // movement controls
    NC__GUI_CONTROL_LEFT,
    NC__GUI_CONTROL_RIGHT,
    NC__GUI_CONTROL_FORWARD,
    NC__GUI_CONTROL_BACKWARD,
    NC__GUI_CONTROL_UP,
    NC__GUI_CONTROL_DOWN,

    // action controls
    NC__GUI_CONTROL_PLACE_BLOCK,
    NC__GUI_CONTROL_REMOVE_BLOCK,

    NC__GUI_CONTROL_COUNT,
    NC__GUI_CONTROL_NONE = NC__GUI_CONTROL_COUNT,
};

typedef uint8_t nc__gui_touch_action_t;
enum {
    NC__GUI_TOUCH_ACTION_MOVE_ANALOG_STICK,
    NC__GUI_TOUCH_ACTION_CAMERA_ANALOG_STICK,
    NC__GUI_TOUCH_ACTION_CAMERA_FREE_DRAG,

    NC__GUI_TOUCH_ACTION_COUNT,
    NC__GUI_TOUCH_ACTION_NONE = NC__GUI_TOUCH_ACTION_COUNT,
};

typedef struct nc__gui_texture_t {
    nc_renderer_texture_t* texture;
    struct nk_image nuklear_image;
} nc__gui_texture_t;

typedef struct nc__gui_vertex_t {
    float position[2];
    float uv[2];
    vkm_ubvec4 color;
} nc__gui_vertex_t;

typedef struct nc__gui_pointer_t {
    SDL_FingerID finger_id;
    vkm_vec2 position;
    vkm_vec2 initial_position;
    nc__gui_control_id_t captured_control;
    nc__gui_touch_action_t touch_action;
    bool active;
} nc__gui_pointer_t;

typedef struct nc_gui_context_t {
    vkm_usvec2 window_size;
    vkm_usvec2 pixel_viewport;
    float window_display_scale;

    struct nk_context nuklear_context;
    struct nk_font_atlas font_atlas;
    struct nk_font* default_font;
    struct nk_draw_null_texture null_texture;
    struct nk_buffer command_buffer;
    struct nk_buffer vertex_buffer;
    struct nk_buffer index_buffer;
    bool nuklear_initialized;
    bool font_atlas_initialized;

    nc_renderer_buffer_t* vertex_gpu_buffer;
    nc_renderer_buffer_t* index_gpu_buffer;

    nc__gui_texture_t font_texture;
    nc__gui_texture_t control_textures[NC__GUI_CONTROL_COUNT];

    struct nk_rect control_rects[NC__GUI_CONTROL_COUNT];
    nc_gui_controls_t controls;
    nc_gui_actions_t pending_actions;
    nc__gui_pointer_t touch_points[NC__GUI_MAX_TOUCHES];

    nc_renderer_overlay_draw_command_t* draw_commands;
    uint32_t draw_command_count;
    uint32_t draw_command_capacity;
    bool draw_ready;
    bool overlay_dirty;

    vkm_vec2 look_delta;
    float mode_switch_accumulator;
    bool touch_controls_enabled;
} nc_gui_context_t;

static const struct nk_draw_vertex_layout_element nc__gui_vertex_layout[] = {
    { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(nc__gui_vertex_t, position) },
    { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(nc__gui_vertex_t, uv) },
    { NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(nc__gui_vertex_t, color) },
    { NK_VERTEX_LAYOUT_END },
};

static const char* nc__gui_control_texture_paths[NC__GUI_CONTROL_COUNT] = {
    NC__GUI_ASSETS_BASE_PATH "textures/gui/left"        NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/right"       NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/up"          NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/down"        NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/up"          NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/down"        NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/place"       NC__GUI_TEXTURE_EXTENSION,
    NC__GUI_ASSETS_BASE_PATH "textures/gui/remove"      NC__GUI_TEXTURE_EXTENSION,
};

static bool nc__gui_point_in_rect(const struct nk_rect rect, const float x, const float y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static bool nc__gui_is_movement_control(const nc__gui_control_id_t control_id) {
    return control_id <= NC__GUI_CONTROL_DOWN;
}

static nc_gui_controls_t nc__gui_control_flag(const nc__gui_control_id_t control_id) {
    static const nc_gui_controls_t flags[] = {
        [NC__GUI_CONTROL_LEFT] =        NC_GUI_CONTROL_MOVE_LEFT,
        [NC__GUI_CONTROL_RIGHT] =       NC_GUI_CONTROL_MOVE_RIGHT,
        [NC__GUI_CONTROL_FORWARD] =     NC_GUI_CONTROL_MOVE_FORWARD,
        [NC__GUI_CONTROL_BACKWARD] =    NC_GUI_CONTROL_MOVE_BACKWARD,
        [NC__GUI_CONTROL_UP] =          NC_GUI_CONTROL_MOVE_UP,
        [NC__GUI_CONTROL_DOWN] =        NC_GUI_CONTROL_MOVE_DOWN,
    };

    NC_ASSERT(control_id < NC__GUI_CONTROL_COUNT);
    return nc__gui_is_movement_control(control_id) ? flags[control_id] : 0;
}

static uint32_t nc__gui_next_capacity(const uint32_t current, const uint32_t required) {
    uint32_t capacity = current ? current : 16;
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

static void nc__gui_reserve_draw_commands(nc_gui_context_t* context, const uint32_t additional_commands) {
    NC_ASSERT(additional_commands);

    const uint32_t required = context->draw_command_count + additional_commands;
    NC_ASSERT(required > context->draw_command_count);

    if (required <= context->draw_command_capacity) {
        return;
    }

    const uint32_t new_capacity = nc__gui_next_capacity(context->draw_command_capacity, required);
    context->draw_commands = realloc(context->draw_commands, new_capacity * sizeof(*context->draw_commands));
    context->draw_command_capacity = new_capacity;
}

static void nc__gui_destroy_texture(nc_renderer_t* renderer, nc__gui_texture_t* texture) {
    if (!texture->texture) {
        return;
    }

    nc_renderer_destroy_texture(renderer, texture->texture);
    *texture = (nc__gui_texture_t){ 0 };
}

static bool nc__gui_load_texture(nc_renderer_t* renderer, const char* path, nc__gui_texture_t* texture) {
    texture->texture = nc_renderer_create_texture_2d_from_file(renderer, path);
    if (!texture->texture) {
        goto error;
    }

    texture->nuklear_image = nk_image_ptr(texture->texture);
    return true;

error:
    return false;
}

static void nc__gui_layout_controls(nc_gui_context_t* context) {
    const float viewport_width = (float)context->pixel_viewport.x;
    const float viewport_height = (float)context->pixel_viewport.y;
    const float button_size = NC__GUI_BUTTON_SIZE_AT_SCALE_1 * context->window_display_scale;
    const float gap = button_size * 0.12f;
    const float margin = button_size * 0.35f;

    const float dpad_left = margin;
    const float dpad_top = viewport_height - margin - button_size * 3.0f - gap * 2.0f;

    context->control_rects[NC__GUI_CONTROL_FORWARD] = nk_rect(
            dpad_left + button_size + gap,
            dpad_top,
            button_size,
            button_size);
    context->control_rects[NC__GUI_CONTROL_LEFT] = nk_rect(
            dpad_left,
            dpad_top + button_size + gap,
            button_size,
            button_size);
    context->control_rects[NC__GUI_CONTROL_BACKWARD] = nk_rect(
            dpad_left + button_size + gap,
            dpad_top + (button_size + gap) * 2.0f,
            button_size,
            button_size);
    context->control_rects[NC__GUI_CONTROL_RIGHT] = nk_rect(
            dpad_left + (button_size + gap) * 2.0f,
            dpad_top + button_size + gap,
            button_size,
            button_size);

    const float right_column_left = viewport_width - margin - button_size;
    const float right_column_top = viewport_height - margin - button_size * 2.0f - gap;
    const float action_row_left = viewport_width - margin - button_size * 2.0f - gap;
    const float action_row_top = right_column_top - button_size * 1.45f - gap * 2.0f;

    context->control_rects[NC__GUI_CONTROL_PLACE_BLOCK] = nk_rect(
            action_row_left,
            action_row_top,
            button_size,
            button_size);
    context->control_rects[NC__GUI_CONTROL_REMOVE_BLOCK] = nk_rect(
            action_row_left + button_size + gap,
            action_row_top,
            button_size,
            button_size);

    context->control_rects[NC__GUI_CONTROL_UP] = nk_rect(
            right_column_left,
            right_column_top,
            button_size,
            button_size);
    context->control_rects[NC__GUI_CONTROL_DOWN] = nk_rect(
            right_column_left,
            right_column_top + button_size + gap,
            button_size,
            button_size);
}

static bool nc__gui_is_control_visible(nc__gui_control_id_t control_id);

static void nc__gui_refresh_controls(nc_gui_context_t* context) {
    nc_gui_controls_t controls = 0;

    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        const nc__gui_pointer_t* pointer = &context->touch_points[i];
        if (!pointer->active || !nc__gui_is_movement_control(pointer->captured_control)) {
            continue;
        }

        const float x = pointer->position.x * (float)context->pixel_viewport.x;
        const float y = pointer->position.y * (float)context->pixel_viewport.y;
        if (nc__gui_point_in_rect(context->control_rects[pointer->captured_control], x, y)) {
            controls |= nc__gui_control_flag(pointer->captured_control);
        }
    }

    context->controls = controls;
}

static nc__gui_control_id_t nc__gui_hit_test_control(const nc_gui_context_t* context, const float x, const float y) {
    for (uint8_t i = 0; i < (uint8_t)NC__GUI_CONTROL_COUNT; i++) {
        if (nc__gui_is_control_visible(i) && nc__gui_point_in_rect(context->control_rects[i], x, y)) {
            return i;
        }
    }

    return NC__GUI_CONTROL_NONE;
}

static nc__gui_pointer_t* nc__gui_find_touch(nc_gui_context_t* context, const SDL_FingerID finger_id) {
    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        if (context->touch_points[i].active && context->touch_points[i].finger_id == finger_id) {
            return &context->touch_points[i];
        }
    }

    return NULL;
}

static nc__gui_pointer_t* nc__gui_alloc_touch(nc_gui_context_t* context) {
    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        if (!context->touch_points[i].active) {
            return &context->touch_points[i];
        }
    }

    return NULL;
}

static float nc__gui_analog_stick_ring_radius(const nc_gui_context_t* context) {
    return NC__GUI_ANALOG_STICK_RING_RADIUS_AT_SCALE_1 * context->window_display_scale;
}

static bool nc__gui_uses_movement_buttons(void) {
    return nc_config_get_touch_movement_mode() == NC_TOUCH_MOVEMENT_MODE_BUTTONS;
}

static bool nc__gui_uses_camera_analog_stick(void) {
    return nc_config_get_touch_camera_mode() == NC_TOUCH_CAMERA_MODE_ANALOG;
}

static bool nc__gui_is_planar_movement_control(const nc__gui_control_id_t control_id) {
    return     control_id == NC__GUI_CONTROL_LEFT
            || control_id == NC__GUI_CONTROL_RIGHT
            || control_id == NC__GUI_CONTROL_FORWARD
            || control_id == NC__GUI_CONTROL_BACKWARD;
}

static bool nc__gui_is_control_visible(const nc__gui_control_id_t control_id) {
    return nc__gui_uses_movement_buttons() || !nc__gui_is_planar_movement_control(control_id);
}

static bool nc__gui_is_touch_action_captured(
    const nc_gui_context_t* context,
    const nc__gui_touch_action_t touch_action
) {
    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        if (context->touch_points[i].active && context->touch_points[i].touch_action == touch_action) {
            return true;
        }
    }

    return false;
}

static nc__gui_touch_action_t nc__gui_touch_action_for_touch(const nc_gui_context_t* context, const float x) {
    nc__gui_touch_action_t touch_action = nc__gui_uses_camera_analog_stick()
            ? NC__GUI_TOUCH_ACTION_CAMERA_ANALOG_STICK
            : NC__GUI_TOUCH_ACTION_CAMERA_FREE_DRAG;

    if (!nc__gui_uses_movement_buttons() && x < (float)context->pixel_viewport.x * 0.5f) {
        touch_action = NC__GUI_TOUCH_ACTION_MOVE_ANALOG_STICK;
    }

    return nc__gui_is_touch_action_captured(context, touch_action) ?
            NC__GUI_TOUCH_ACTION_NONE :
            touch_action;
}

static vkm_vec2 nc__gui_pointer_pixel_position(const nc_gui_context_t* context, const nc__gui_pointer_t* pointer) {
    return (vkm_vec2){ {
        pointer->position.x * (float)context->pixel_viewport.x,
        pointer->position.y * (float)context->pixel_viewport.y,
    } };
}

static vkm_vec2 nc__gui_pointer_start_pixel_position(
    const nc_gui_context_t* context,
    const nc__gui_pointer_t* pointer
) {
    return (vkm_vec2){ {
        pointer->initial_position.x * (float)context->pixel_viewport.x,
        pointer->initial_position.y * (float)context->pixel_viewport.y,
    } };
}

static vkm_vec2 nc__gui_get_clamped_analog_stick_position(
    const nc_gui_context_t* context,
    const nc__gui_pointer_t* pointer
) {
    const vkm_vec2 ring_position = nc__gui_pointer_start_pixel_position(context, pointer);
    const vkm_vec2 pointer_position = nc__gui_pointer_pixel_position(context, pointer);
    vkm_vec2 delta;
    vkm_sub(&pointer_position, &ring_position, &delta);

    const float radius = nc__gui_analog_stick_ring_radius(context);
    const float length = vkm_length(&delta);
    if (length > radius && length > 0.0f) {
        vkm_mul(&delta, radius / length, &delta);
    }

    vkm_vec2 result;
    vkm_add(&ring_position, &delta, &result);
    return result;
}

static bool nc__gui_get_analog_stick_delta(
        const nc_gui_context_t* context,
        const nc__gui_touch_action_t touch_action,
        vkm_vec2* delta
) {
    *delta = (vkm_vec2){ { 0.0f, 0.0f } };

    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        const nc__gui_pointer_t* pointer = &context->touch_points[i];
        if (!pointer->active || pointer->touch_action != touch_action) {
            continue;
        }

        const vkm_vec2 ring_position = nc__gui_pointer_start_pixel_position(context, pointer);
        const vkm_vec2 stick_position = nc__gui_get_clamped_analog_stick_position(context, pointer);
        vkm_sub(&stick_position, &ring_position, delta);

        const float radius = nc__gui_analog_stick_ring_radius(context);
        if (radius > 0.0f) {
            delta->x /= radius;
            delta->y /= -radius;
        }
        return true;
    }

    return false;
}

static void nc__gui_clear_touch_state(nc_gui_context_t* context) {
    memset(context->touch_points, 0, sizeof(context->touch_points));
    context->look_delta = (vkm_vec2){ { 0.0f, 0.0f } };
    nc__gui_refresh_controls(context);
}

static bool nc__gui_is_control_pressed(const nc_gui_context_t* context, const nc__gui_control_id_t control_id) {
    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        const nc__gui_pointer_t* pointer = &context->touch_points[i];
        const float x = pointer->position.x * (float)context->pixel_viewport.x;
        const float y = pointer->position.y * (float)context->pixel_viewport.y;
        if (pointer->active &&
                pointer->captured_control == control_id &&
                nc__gui_point_in_rect(context->control_rects[control_id], x, y)) {
            return true;
        }
    }

    return false;
}

static void nc__gui_push_action(nc_gui_context_t* context, const nc__gui_control_id_t control_id) {
    switch (control_id) {
        case NC__GUI_CONTROL_PLACE_BLOCK:
            context->pending_actions |= NC_GUI_ACTION_PLACE_BLOCK;
            break;
        case NC__GUI_CONTROL_REMOVE_BLOCK:
            context->pending_actions |= NC_GUI_ACTION_REMOVE_BLOCK;
            break;
        default:
            break;
    }
}

static void nc__gui_build_overlay(nc_gui_context_t* context) {
    struct nk_context* nuklear = &context->nuklear_context;
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
            nk_rect(0.0f, 0.0f, (float)context->pixel_viewport.x, (float)context->pixel_viewport.y),
            NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT)) {
        struct nk_command_buffer* canvas = nk_window_get_canvas(nuklear);

        if (context->touch_controls_enabled) {
            for (uint8_t i = 0; i < (uint8_t)NC__GUI_CONTROL_COUNT; i++) {
                if (!nc__gui_is_control_visible(i)) {
                    continue;
                }

                const struct nk_rect rect = context->control_rects[i];
                const bool pressed = nc__gui_is_control_pressed(context, i);
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
    }
    nk_end(nuklear);

    nuklear->style.window = saved_window_style;
}

static bool nc__is_touchscreen_available(void) {
    int count = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&count);

    if (devices) {
        SDL_free(devices);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        SDL_ClearError();
    }

    return count > 0;
}

nc_gui_context_t* nc_gui_init(nc_renderer_t* renderer) {
    bool atlas_began = false;
    nc_gui_context_t* result = calloc(1, sizeof(*result));

    result->window_size = nc_renderer_get_window_size(renderer);
    result->pixel_viewport = nc_renderer_get_viewport(renderer);

    nk_buffer_init_default(&result->command_buffer);
    nk_buffer_init_default(&result->vertex_buffer);
    nk_buffer_init_default(&result->index_buffer);

    nk_font_atlas_init_default(&result->font_atlas);
    result->font_atlas_initialized = true;
    nk_font_atlas_begin(&result->font_atlas);
    atlas_began = true;

    result->default_font = nk_font_atlas_add_default(&result->font_atlas, 18.0f, NULL);
    NC_CHECK_RESULT(result->default_font, "nk_font_atlas_add_default() failed.");

    int font_width = 0;
    int font_height = 0;
    const void* font_pixels = nk_font_atlas_bake(&result->font_atlas, &font_width, &font_height, NK_FONT_ATLAS_RGBA32);
    NC_CHECK_RESULT(font_pixels, "nk_font_atlas_bake() failed.");

    NC_ASSERT(font_width <= INT16_MAX);
    NC_ASSERT(font_height <= INT16_MAX);

    result->font_texture.texture = nc_renderer_create_rgba_texture_2d(
            renderer,
            (int16_t)font_width,
            (int16_t)font_height,
            font_pixels);
    NC_CHECK_RESULT(result->font_texture.texture, "Failed to create the Nuklear font texture.");
    result->font_texture.nuklear_image = nk_image_ptr(result->font_texture.texture);

    nk_font_atlas_end(&result->font_atlas, nk_handle_ptr(result->font_texture.texture), &result->null_texture);
    atlas_began = false;

    const bool nuklear_result = nk_init_default(&result->nuklear_context, &result->default_font->handle);
    NC_CHECK_RESULT(nuklear_result, "nk_init_default() failed.");
    result->nuklear_initialized = true;

    for (int i = 0; i < NC__GUI_CONTROL_COUNT; i++) {
        if (!nc__gui_load_texture(renderer, nc__gui_control_texture_paths[i], &result->control_textures[i])) {
            goto error;
        }
    }

    result->vertex_gpu_buffer = nc_renderer_create_buffer(
            renderer,
            NC_RENDERER_BUFFER_USAGE_VERTEX,
            NC__GUI_INITIAL_BUFFER_CAPACITY);
    NC_CHECK_RESULT(result->vertex_gpu_buffer, "Failed to create the GUI vertex buffer.");

    result->index_gpu_buffer = nc_renderer_create_buffer(
            renderer,
            NC_RENDERER_BUFFER_USAGE_INDEX,
            NC__GUI_INITIAL_BUFFER_CAPACITY);
    NC_CHECK_RESULT(result->index_gpu_buffer, "Failed to create the GUI index buffer.");

    result->window_display_scale = nc_renderer_get_window_display_scale(renderer);
    result->touch_controls_enabled = nc__is_touchscreen_available();

    nc_gui_set_window_size(result, result->window_size.x, result->window_size.y);
    nc_gui_set_pixel_viewport(result, result->pixel_viewport.x, result->pixel_viewport.y);

    return result;

error:
    if (atlas_began) {
        nk_font_atlas_end(&result->font_atlas, nk_handle_ptr(result->font_texture.texture), &result->null_texture);
    }
    nc_gui_fini(result, renderer);
    return NULL;
}

void nc_gui_set_window_size(nc_gui_context_t* context, const uint16_t width, const uint16_t height) {
    context->window_size.x = width;
    context->window_size.y = height;
}

void nc_gui_set_pixel_viewport(nc_gui_context_t* context, const uint16_t width, const uint16_t height) {
    context->pixel_viewport.x = width;
    context->pixel_viewport.y = height;
    nc__gui_layout_controls(context);
    nc__gui_refresh_controls(context);
    context->overlay_dirty = true;
}

void nc_gui_set_window_display_scale(nc_gui_context_t* context, const float window_display_scale) {
    context->window_display_scale = window_display_scale;
    nc__gui_layout_controls(context);
    nc__gui_refresh_controls(context);
    context->overlay_dirty = true;
}

bool nc_gui_handle_event(nc_gui_context_t* context, const SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_FINGER_DOWN: {
            if (!context->touch_controls_enabled) {
                return false;
            }

            const float x = event->tfinger.x * (float)context->pixel_viewport.x;
            const float y = event->tfinger.y * (float)context->pixel_viewport.y;
            const nc__gui_control_id_t control_id = nc__gui_hit_test_control(
                    context,
                    x,
                    y);
            const nc__gui_touch_action_t touch_action = control_id == NC__GUI_CONTROL_NONE
                    ? nc__gui_touch_action_for_touch(context, x)
                    : NC__GUI_TOUCH_ACTION_NONE;
            if (control_id == NC__GUI_CONTROL_NONE && touch_action == NC__GUI_TOUCH_ACTION_NONE) {
                return false;
            }

            nc__gui_pointer_t* pointer = nc__gui_alloc_touch(context);
            if (!pointer) {
                return false;
            }

            *pointer = (nc__gui_pointer_t){
                .finger_id = event->tfinger.fingerID,
                .position = { { event->tfinger.x, event->tfinger.y } },
                .initial_position = { { event->tfinger.x, event->tfinger.y } },
                .captured_control = control_id,
                .touch_action = touch_action,
                .active = true,
            };
            if (control_id != NC__GUI_CONTROL_NONE) {
                nc__gui_refresh_controls(context);
                context->overlay_dirty = true;
            }
            return true;
        }
        case SDL_EVENT_FINGER_MOTION: {
            if (!context->touch_controls_enabled) {
                const vkm_vec2 delta = { { event->tfinger.dx, event->tfinger.dy } };
                const float length = vkm_length(&delta);
                // Some arbitrary factor to increase sensitivity.
                context->mode_switch_accumulator += length * 2.0f;
                return false;
            }

            nc__gui_pointer_t* pointer = nc__gui_find_touch(context, event->tfinger.fingerID);
            if (!pointer) {
                return false;
            }

            pointer->position.x = event->tfinger.x;
            pointer->position.y = event->tfinger.y;
            if (pointer->touch_action == NC__GUI_TOUCH_ACTION_CAMERA_FREE_DRAG) {
                context->look_delta.x +=
                        event->tfinger.dx * (float)context->pixel_viewport.x / context->window_display_scale;
                context->look_delta.y +=
                        event->tfinger.dy * (float)context->pixel_viewport.y / context->window_display_scale;
            } else if (pointer->captured_control != NC__GUI_CONTROL_NONE) {
                nc__gui_refresh_controls(context);
                context->overlay_dirty = true;
            }
            return true;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            nc__gui_pointer_t* pointer = nc__gui_find_touch(context, event->tfinger.fingerID);
            if (!pointer) {
                return false;
            }

            pointer->position.x = event->tfinger.x;
            pointer->position.y = event->tfinger.y;
            if (event->type == SDL_EVENT_FINGER_UP &&
                    pointer->captured_control != NC__GUI_CONTROL_NONE &&
                    nc__gui_point_in_rect(
                            context->control_rects[pointer->captured_control],
                            pointer->position.x * (float)context->pixel_viewport.x,
                            pointer->position.y * (float)context->pixel_viewport.y)) {
                nc__gui_push_action(context, pointer->captured_control);
            }

            *pointer = (nc__gui_pointer_t){ 0 };
            nc__gui_refresh_controls(context);
            context->overlay_dirty = true;
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            nc__gui_clear_touch_state(context);
            context->overlay_dirty = true;
            context->touch_controls_enabled = false;
            context->mode_switch_accumulator = 0.0f;
            return false;
        case SDL_EVENT_MOUSE_MOTION: {
            if (!context->touch_controls_enabled) {
                return false;
            }

            const vkm_vec2 delta = { { event->motion.xrel, event->motion.yrel } };
            const float length = vkm_length(&delta);
            // Some arbitrary factor to reduce sensitivity.
            context->mode_switch_accumulator += length * 0.002f;
            return false;
        }
        default:
            return false;
    }
}

nc_gui_controls_t nc_gui_get_controls(const nc_gui_context_t* context) {
    return context->controls;
}

void nc_gui_get_movement_delta(const nc_gui_context_t* context, vkm_vec2* delta) {
    nc__gui_get_analog_stick_delta(context, NC__GUI_TOUCH_ACTION_MOVE_ANALOG_STICK, delta);
}

void nc_gui_get_camera_delta(const nc_gui_context_t* context, vkm_vec2* delta) {
    nc__gui_get_analog_stick_delta(context, NC__GUI_TOUCH_ACTION_CAMERA_ANALOG_STICK, delta);
}

nc_gui_actions_t nc_gui_consume_actions(nc_gui_context_t* context) {
    const nc_gui_actions_t actions = context->pending_actions;
    context->pending_actions = 0;
    return actions;
}

bool nc_gui_consume_look_delta(nc_gui_context_t* context, vkm_vec2* delta) {
    const bool result = context->look_delta.x != 0.0f || context->look_delta.y != 0.0f;

    *delta = context->look_delta;
    context->look_delta = (vkm_vec2){ { 0.0f, 0.0f } };

    return result;
}

bool nc_gui_is_touch_captured(const nc_gui_context_t* context, const SDL_FingerID finger_id) {
    for (int i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        if (context->touch_points[i].active && context->touch_points[i].finger_id == finger_id) {
            return true;
        }
    }

    return false;
}

bool nc_gui_prepare_frame(nc_gui_context_t* context, nc_renderer_t* renderer, const float delta_time) {
    // Automagically enable or disable touchscreen controls when the player starts using a touchscreen
    // or keyboard & mouse, respectively. But only do so when the player looks around fast enough,
    // additionally to directly pressing a key or using the mouse wheel.
    // Looking around increases the accumulator (see the event handler in this file).
    // Every frame the accumulator is decreased by the delta time.
    // Do the switching when the input accumulator reaches the threshold.

    context->mode_switch_accumulator -= delta_time;

    if (context->mode_switch_accumulator > NC__TOUCH_CONTROLS_SWITCH_THRESHOLD) {
        context->touch_controls_enabled = !context->touch_controls_enabled;
        context->mode_switch_accumulator = 0.0f;
        context->overlay_dirty = true;

        if (!context->touch_controls_enabled) {
            // Clear all current touch actions.
            nc__gui_clear_touch_state(context);
        }
    } else if (context->mode_switch_accumulator < 0.0f) {
        context->mode_switch_accumulator = 0.0f;
    }

    if (!context->overlay_dirty && context->draw_ready) {
        return true;
    }

    context->draw_command_count = 0;
    context->draw_ready = false;

    nk_clear(&context->nuklear_context);
    nk_buffer_clear(&context->command_buffer);
    nk_buffer_clear(&context->vertex_buffer);
    nk_buffer_clear(&context->index_buffer);
    nc__gui_build_overlay(context);

    const nk_flags convert_result = nk_convert(
            &context->nuklear_context,
            &context->command_buffer,
            &context->vertex_buffer,
            &context->index_buffer,
            &(struct nk_convert_config){
                .global_alpha = 1.0f,
                .line_AA = NK_ANTI_ALIASING_OFF,
                .shape_AA = NK_ANTI_ALIASING_OFF,
                .circle_segment_count = 22,
                .arc_segment_count = 22,
                .curve_segment_count = 22,
                .tex_null = context->null_texture,
                .vertex_layout = nc__gui_vertex_layout,
                .vertex_size = sizeof(nc__gui_vertex_t),
                .vertex_alignment = _Alignof(nc__gui_vertex_t),
            });
    if (convert_result != NK_CONVERT_SUCCESS) {
        NC_SET_ERROR("nk_convert() failed with code %u", convert_result);
        goto error;
    }

    uint32_t first_index = 0;
    for (const struct nk_draw_command* draw_command = nk__draw_begin(&context->nuklear_context, &context->command_buffer);
         draw_command;
         draw_command = nk__draw_next(draw_command, &context->command_buffer, &context->nuklear_context)) {
        if (draw_command->elem_count == 0 || !draw_command->texture.ptr) {
            first_index += draw_command->elem_count;
            continue;
        }

        const int clip_x = (int)vkm_max(draw_command->clip_rect.x, 0.0f);
        const int clip_y = (int)vkm_max(draw_command->clip_rect.y, 0.0f);
        const int clip_right = (int)vkm_min(draw_command->clip_rect.x + draw_command->clip_rect.w, (float)context->pixel_viewport.x);
        const int clip_bottom = (int)vkm_min(draw_command->clip_rect.y + draw_command->clip_rect.h, (float)context->pixel_viewport.y);

        if (clip_right > clip_x && clip_bottom > clip_y) {
            nc__gui_reserve_draw_commands(context, 1);
            context->draw_commands[context->draw_command_count++] = (nc_renderer_overlay_draw_command_t){
                .texture = (const nc_renderer_texture_t*)draw_command->texture.ptr,
                .clip_rect = {
                    .x = clip_x,
                    .y = clip_y,
                    .w = clip_right - clip_x,
                    .h = clip_bottom - clip_y,
                },
                .element_count = draw_command->elem_count,
                .first_index = first_index,
            };
        }

        first_index += draw_command->elem_count;
    }

    if (context->draw_command_count > 0) {
        const uint32_t vertex_bytes = (uint32_t)nk_buffer_total(&context->vertex_buffer);
        const uint32_t index_bytes = (uint32_t)nk_buffer_total(&context->index_buffer);

        if (!nc_renderer_queue_buffer_upload(
                renderer,
                context->vertex_gpu_buffer,
                nk_buffer_memory_const(&context->vertex_buffer),
                vertex_bytes)) {
            goto error;
        }

        if (!nc_renderer_queue_buffer_upload(
                renderer,
                context->index_gpu_buffer,
                nk_buffer_memory_const(&context->index_buffer),
                index_bytes)) {
            goto error;
        }

        context->draw_ready = true;
    }

    nk_clear(&context->nuklear_context);
    context->overlay_dirty = false;
    return true;

error:
    nk_clear(&context->nuklear_context);
    return false;
}

void nc_gui_get_overlay_draw(const nc_gui_context_t* context, nc_renderer_overlay_draw_t* draw) {
    *draw = (nc_renderer_overlay_draw_t){
        .vertex_buffer = context->vertex_gpu_buffer,
        .index_buffer = context->index_gpu_buffer,
        .draw_commands = context->draw_commands,
        .draw_command_count = context->draw_ready ? context->draw_command_count : 0,
    };
}

void nc_gui_get_procedural_overlay_draw(const nc_gui_context_t* context, nc_renderer_procedural_overlay_draw_t* draw) {
    *draw = (nc_renderer_procedural_overlay_draw_t){
        .analog_stick_ring_radius = nc__gui_analog_stick_ring_radius(context),
        .analog_stick_ring_thickness = NC__GUI_ANALOG_STICK_RING_THICKNESS_AT_SCALE_1 * context->window_display_scale,
        .analog_stick_radius = NC__GUI_ANALOG_STICK_RADIUS_AT_SCALE_1 * context->window_display_scale,
        .crosshair_size = NC__GUI_CROSSHAIR_SIZE_AT_SCALE_1 * context->window_display_scale,
    };

    if (!context->touch_controls_enabled) {
        return;
    }

    for (size_t i = 0; i < NC__GUI_MAX_TOUCHES; i++) {
        const nc__gui_pointer_t* pointer = &context->touch_points[i];
        if (!pointer->active) {
            continue;
        }

        uint8_t analog_stick = 0;
        switch (pointer->touch_action) {
            case NC__GUI_TOUCH_ACTION_MOVE_ANALOG_STICK:
                analog_stick = 0;
                break;
            case NC__GUI_TOUCH_ACTION_CAMERA_ANALOG_STICK:
                analog_stick = 1;
                break;
            default:
                continue;
        }

        draw->analog_sticks_active[analog_stick] = true;
        draw->analog_stick_ring_positions[analog_stick] = nc__gui_pointer_start_pixel_position(context, pointer);
        draw->analog_stick_positions[analog_stick] = nc__gui_get_clamped_analog_stick_position(context, pointer);
    }
}

void nc_gui_fini(nc_gui_context_t* context, nc_renderer_t* renderer) {
    if (!context) {
        return;
    }

    nc_renderer_destroy_buffer(renderer, context->index_gpu_buffer);
    nc_renderer_destroy_buffer(renderer, context->vertex_gpu_buffer);

    for (int i = 0; i < NC__GUI_CONTROL_COUNT; i++) {
        nc__gui_destroy_texture(renderer, &context->control_textures[i]);
    }
    nc__gui_destroy_texture(renderer, &context->font_texture);

    free(context->draw_commands);
    if (context->nuklear_initialized) {
        nk_free(&context->nuklear_context);
    }
    if (context->font_atlas_initialized) {
        nk_font_atlas_clear(&context->font_atlas);
    }

    nk_buffer_free(&context->index_buffer);
    nk_buffer_free(&context->vertex_buffer);
    nk_buffer_free(&context->command_buffer);

    free(context);
}
