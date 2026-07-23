#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>
#include <novacube/error_handling.h>
#include <novacube/macros.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>

#define NK_IMPLEMENTATION
#include "nuklear_config.h"

#include "nuklear_backend.h"

#define NC__NUKLEAR_INITIAL_BUFFER_CAPACITY 65536
#define NC__NUKLEAR_BASE_FONT_HEIGHT 18.0f

typedef struct nc__nuklear_vertex_t {
    float position[2];
    float uv[2];
    vkm_ubvec4 color;
} nc__nuklear_vertex_t;

typedef struct nc_nuklear_backend_t {
    struct nk_context context;
    struct nk_font_atlas font_atlas;
    struct nk_font* default_font;
    struct nk_draw_null_texture null_texture;
    struct nk_buffer command_buffer;
    struct nk_buffer vertex_buffer;
    struct nk_buffer index_buffer;
    bool context_initialized;
    bool font_atlas_initialized;

    nc_renderer_texture_t* font_texture;
    nc_renderer_buffer_t* vertex_gpu_buffer;
    nc_renderer_buffer_t* index_gpu_buffer;
    // TODO: Replace with TDS vector.
    nc_renderer_overlay_draw_command_t* draw_commands;
    uint32_t draw_command_count;
    uint32_t draw_command_capacity;
    float font_texture_scale;

    // TODO: Replace all bools with flags.
    bool mouse_captured;
    bool touch_active;
    bool text_input_enabled;
    SDL_FingerID touch_finger;
} nc_nuklear_backend_t;

static const struct nk_draw_vertex_layout_element nc__nuklear_vertex_layout[] = {
    { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(nc__nuklear_vertex_t, position) },
    { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(nc__nuklear_vertex_t, uv) },
    { NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(nc__nuklear_vertex_t, color) },
    { NK_VERTEX_LAYOUT_END },
};

static_assert(sizeof(nk_draw_index) == sizeof(uint32_t), "The renderer expects 32-bit Nuklear indices.");

static uint32_t nc__nuklear_next_capacity(const uint32_t current, const uint32_t required) {
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

static void nc__nuklear_reserve_draw_commands(
    nc_nuklear_backend_t* backend,
    const uint32_t additional_commands
) {
    NC_ASSERT(additional_commands);
    const uint32_t required = backend->draw_command_count + additional_commands;
    NC_ASSERT(required > backend->draw_command_count);
    if (required <= backend->draw_command_capacity) {
        return;
    }

    const uint32_t new_capacity =
            nc__nuklear_next_capacity(backend->draw_command_capacity, required);
    backend->draw_commands = realloc(
            backend->draw_commands,
            new_capacity * sizeof(*backend->draw_commands));
    backend->draw_command_capacity = new_capacity;
}

// TODO: What does this do? Comment, document it.
// Maybe it converts SDL window coords to Nuklear window coords?
static vkm_vec2 nc__nuklear_window_to_gui_position(
    const nc_nuklear_view_t* view,
    const float window_x,
    const float window_y
) {
    if (view->window_size.x == 0 || view->window_size.y == 0 || view->scale <= 0.0f) {
        return (vkm_vec2){ { 0.0f, 0.0f } };
    }

    return (vkm_vec2){ {
        window_x * (float)view->pixel_viewport.x / (float)view->window_size.x / view->scale,
        window_y * (float)view->pixel_viewport.y / (float)view->window_size.y / view->scale,
    } };
}

static vkm_vec2 nc__nuklear_normalized_to_gui_position(
    const nc_nuklear_view_t* view,
    const float normalized_x,
    const float normalized_y
) {
    if (view->scale <= 0.0f) {
        return (vkm_vec2){ { 0.0f, 0.0f } };
    }

    return (vkm_vec2){ {
        normalized_x * (float)view->pixel_viewport.x / view->scale,
        normalized_y * (float)view->pixel_viewport.y / view->scale,
    } };
}

static bool nc__nuklear_point_in_rect(const struct nk_rect rect, const float x, const float y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static bool nc__nuklear_point_in_interactive_window(
    const nc_nuklear_backend_t* backend,
    const float x,
    const float y
) {
    const struct nk_context* context = &backend->context;
    for (const struct nk_window* window = context->end; window; window = window->prev) {
        const nk_flags ignored_flags =
                (nk_flags)NK_WINDOW_HIDDEN |
                (nk_flags)NK_WINDOW_CLOSED |
                (nk_flags)NK_WINDOW_NO_INPUT;
        if (window->flags & ignored_flags) {
            continue;
        }

        if (window->popup.active &&
                window->popup.win &&
                nc__nuklear_point_in_rect(window->popup.win->bounds, x, y)) {
            return true;
        }

        struct nk_rect bounds = window->bounds;
        if (window->flags & NK_WINDOW_MINIMIZED) {
            bounds.h =
                    context->style.font->height +
                    2.0f * context->style.window.header.padding.y +
                    2.0f * context->style.window.header.label_padding.y;
        }
        if (nc__nuklear_point_in_rect(bounds, x, y)) {
            return true;
        }
    }

    return false;
}

static void nc__nuklear_clipboard_copy(nk_handle userdata, const char* text, const int length) {
    (void)userdata;

    char* terminated_text = malloc((size_t)length + 1);
    memcpy(terminated_text, text, (size_t)length);
    terminated_text[length] = '\0';
    if (!SDL_SetClipboardText(terminated_text)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetClipboardText() failed: %s", SDL_GetError());
        SDL_ClearError();
    }
    free(terminated_text);
}

static void nc__nuklear_clipboard_paste(nk_handle userdata, struct nk_text_edit* edit) {
    (void)userdata;

    char* text = SDL_GetClipboardText();
    if (!text) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetClipboardText() failed: %s", SDL_GetError());
        SDL_ClearError();
        return;
    }

    nk_textedit_paste(edit, text, (int)strlen(text));
    SDL_free(text);
}

static bool nc__nuklear_create_font_resources(
    nc_renderer_t* renderer,
    const float texture_scale,
    struct nk_font_atlas* atlas,
    struct nk_font** font,
    struct nk_draw_null_texture* null_texture,
    nc_renderer_texture_t** texture
) {
    nk_font_atlas_init_default(atlas);
    nk_font_atlas_begin(atlas);

    *font = nk_font_atlas_add_default(atlas, NC__NUKLEAR_BASE_FONT_HEIGHT * texture_scale, NULL);
    NC_CHECK_RESULT(*font, "nk_font_atlas_add_default() failed.");

    int font_width = 0;
    int font_height = 0;
    const void* font_pixels = nk_font_atlas_bake(atlas, &font_width, &font_height, NK_FONT_ATLAS_RGBA32);
    NC_CHECK_RESULT(font_pixels, "nk_font_atlas_bake() failed.");
    NC_CHECK_RESULT(
            font_width > 0 && font_width <= INT16_MAX && font_height > 0 && font_height <= INT16_MAX,
            "The Nuklear font atlas is too large.");

    *texture = nc_renderer_create_rgba_texture_2d(
            renderer,
            NC_RENDERER_TEXTURE_TYPE_DATA,
            (int16_t)font_width,
            (int16_t)font_height,
            font_pixels);
    NC_CHECK_RESULT(*texture, "Failed to create the Nuklear font texture.");

    nk_font_atlas_end(atlas, nk_handle_ptr(*texture), null_texture);
    (*font)->handle.height = NC__NUKLEAR_BASE_FONT_HEIGHT;
    return true;

error:
    nk_font_atlas_clear(atlas);
    if (*texture) {
        nc_renderer_destroy_texture(renderer, *texture);
        *texture = NULL;
    }
    *font = NULL;
    return false;
}

static bool nc__nuklear_rebuild_font(
    nc_nuklear_backend_t* backend,
    nc_renderer_t* renderer,
    const float scale
) {
    struct nk_font_atlas new_atlas;
    struct nk_font* new_font = NULL;
    struct nk_draw_null_texture new_null_texture;
    nc_renderer_texture_t* new_texture = NULL;
    if (!nc__nuklear_create_font_resources(
            renderer,
            scale,
            &new_atlas,
            &new_font,
            &new_null_texture,
            &new_texture)) {
        return false;
    }

    struct nk_font_atlas old_atlas = backend->font_atlas;
    nc_renderer_texture_t* old_texture = backend->font_texture;
    backend->font_atlas = new_atlas;
    backend->default_font = new_font;
    backend->null_texture = new_null_texture;
    backend->font_texture = new_texture;
    backend->font_texture_scale = scale;
    nk_style_set_font(&backend->context, &backend->default_font->handle);

    nc_renderer_destroy_texture(renderer, old_texture);
    nk_font_atlas_clear(&old_atlas);
    return true;
}

static void nc__nuklear_motion(nc_nuklear_backend_t* backend, const vkm_vec2 position) {
    nk_input_motion(&backend->context, (int)lroundf(position.x), (int)lroundf(position.y));
}

static enum nk_buttons nc__nuklear_mouse_button(const Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return NK_BUTTON_LEFT;
        case SDL_BUTTON_MIDDLE:
            return NK_BUTTON_MIDDLE;
        case SDL_BUTTON_RIGHT:
            return NK_BUTTON_RIGHT;
        default:
            return NK_BUTTON_MAX;
    }
}

static void nc__nuklear_key(nc_nuklear_backend_t* backend, const SDL_KeyboardEvent* event) {
    struct nk_context* context = &backend->context;
    const bool down = event->type == SDL_EVENT_KEY_DOWN;
    const bool ctrl = (event->mod & SDL_KMOD_CTRL) != 0;

    nk_input_key(context, NK_KEY_SHIFT, (event->mod & SDL_KMOD_SHIFT) != 0);
    nk_input_key(context, NK_KEY_CTRL, ctrl);
    nk_input_key(context, NK_KEY_ALT, (event->mod & SDL_KMOD_ALT) != 0);

    switch (event->scancode) {
        case SDL_SCANCODE_DELETE:
            nk_input_key(context, NK_KEY_DEL, down);
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            nk_input_key(context, NK_KEY_ENTER, down);
            break;
        case SDL_SCANCODE_TAB:
            nk_input_key(context, NK_KEY_TAB, down);
            break;
        case SDL_SCANCODE_BACKSPACE:
            nk_input_key(context, NK_KEY_BACKSPACE, down);
            break;
        case SDL_SCANCODE_UP:
            nk_input_key(context, NK_KEY_UP, down);
            break;
        case SDL_SCANCODE_DOWN:
            nk_input_key(context, NK_KEY_DOWN, down);
            break;
        case SDL_SCANCODE_LEFT:
            nk_input_key(context, NK_KEY_LEFT, down && !ctrl);
            nk_input_key(context, NK_KEY_TEXT_WORD_LEFT, down && ctrl);
            break;
        case SDL_SCANCODE_RIGHT:
            nk_input_key(context, NK_KEY_RIGHT, down && !ctrl);
            nk_input_key(context, NK_KEY_TEXT_WORD_RIGHT, down && ctrl);
            break;
        case SDL_SCANCODE_HOME:
            nk_input_key(context, NK_KEY_TEXT_LINE_START, down && !ctrl);
            nk_input_key(context, NK_KEY_TEXT_START, down && ctrl);
            nk_input_key(context, NK_KEY_SCROLL_START, down);
            break;
        case SDL_SCANCODE_END:
            nk_input_key(context, NK_KEY_TEXT_LINE_END, down && !ctrl);
            nk_input_key(context, NK_KEY_TEXT_END, down && ctrl);
            nk_input_key(context, NK_KEY_SCROLL_END, down);
            break;
        case SDL_SCANCODE_PAGEUP:
            nk_input_key(context, NK_KEY_SCROLL_UP, down);
            break;
        case SDL_SCANCODE_PAGEDOWN:
            nk_input_key(context, NK_KEY_SCROLL_DOWN, down);
            break;
        case SDL_SCANCODE_INSERT:
            nk_input_key(context, NK_KEY_TEXT_INSERT_MODE, down);
            break;
        case SDL_SCANCODE_C:
            nk_input_key(context, NK_KEY_COPY, down && ctrl);
            break;
        case SDL_SCANCODE_X:
            nk_input_key(context, NK_KEY_CUT, down && ctrl);
            break;
        case SDL_SCANCODE_V:
            nk_input_key(context, NK_KEY_PASTE, down && ctrl);
            break;
        case SDL_SCANCODE_Z:
            nk_input_key(context, NK_KEY_TEXT_UNDO, down && ctrl);
            break;
        case SDL_SCANCODE_Y:
            nk_input_key(context, NK_KEY_TEXT_REDO, down && ctrl);
            break;
        case SDL_SCANCODE_A:
            nk_input_key(context, NK_KEY_TEXT_SELECT_ALL, down && ctrl);
            break;
        default:
            if (event->scancode >= SDL_SCANCODE_F1 && event->scancode <= SDL_SCANCODE_F12) {
                nk_input_key(
                        context,
                        (enum nk_keys)(NK_KEY_F1 + event->scancode - SDL_SCANCODE_F1),
                        down);
            }
            break;
    }
}

static void nc__nuklear_release_mouse(
    nc_nuklear_backend_t* backend,
    const bool park_cursor
) {
    struct nk_context* context = &backend->context;
    const int x = (int)lroundf(context->input.mouse.pos.x);
    const int y = (int)lroundf(context->input.mouse.pos.y);
    for (enum nk_buttons button = NK_BUTTON_LEFT; button < NK_BUTTON_MAX; button++) {
        nk_input_button(context, button, x, y, false);
    }
    if (park_cursor) {
        nk_input_motion(context, -1, -1);
    }
    backend->mouse_captured = false;
}

static void nc__nuklear_release_input(nc_nuklear_backend_t* backend) {
    nc__nuklear_release_mouse(backend, false);
    for (enum nk_keys key = NK_KEY_SHIFT; key < NK_KEY_MAX; key++) {
        nk_input_key(&backend->context, key, false);
    }
    backend->touch_active = false;
}

static bool nc__nuklear_is_synthetic_touch_mouse_event(const SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
            return event->motion.which == SDL_TOUCH_MOUSEID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event->button.which == SDL_TOUCH_MOUSEID;
        case SDL_EVENT_MOUSE_WHEEL:
            return event->wheel.which == SDL_TOUCH_MOUSEID;
        default:
            return false;
    }
}

static bool nc__nuklear_is_mouse_event(const SDL_Event* event) {
    return     event->type == SDL_EVENT_MOUSE_MOTION
            || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN
            || event->type == SDL_EVENT_MOUSE_BUTTON_UP
            || event->type == SDL_EVENT_MOUSE_WHEEL
            || event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE;
}

static void nc__nuklear_finish_frame(nc_nuklear_backend_t* backend) {
    nk_clear(&backend->context);
    nk_input_begin(&backend->context);
}

nc_nuklear_backend_t* nc_nuklear_backend_init(nc_renderer_t* renderer, const float scale) {
    nc_nuklear_backend_t* backend = calloc(1, sizeof(*backend));
    nk_buffer_init_default(&backend->command_buffer);
    nk_buffer_init_default(&backend->vertex_buffer);
    nk_buffer_init_default(&backend->index_buffer);

    if (!nc__nuklear_create_font_resources(
            renderer,
            scale,
            &backend->font_atlas,
            &backend->default_font,
            &backend->null_texture,
            &backend->font_texture)) {
        goto error;
    }
    backend->font_atlas_initialized = true;
    backend->font_texture_scale = scale;

    NC_CHECK_RESULT(
            nk_init_default(&backend->context, &backend->default_font->handle),
            "nk_init_default() failed.");
    backend->context_initialized = true;
    backend->context.clip.copy = nc__nuklear_clipboard_copy;
    backend->context.clip.paste = nc__nuklear_clipboard_paste;
    backend->context.clip.userdata = nk_handle_ptr(backend);
    backend->context.style.slider.padding.x = vkm_max(
            backend->context.style.slider.padding.x,
            backend->context.style.slider.cursor_size.x * 0.5f);
    nk_input_begin(&backend->context);

    backend->vertex_gpu_buffer = nc_renderer_create_buffer(
            renderer,
            NC_RENDERER_BUFFER_USAGE_VERTEX,
            NC__NUKLEAR_INITIAL_BUFFER_CAPACITY);
    NC_CHECK_RESULT(backend->vertex_gpu_buffer, "Failed to create the Nuklear vertex buffer.");
    backend->index_gpu_buffer = nc_renderer_create_buffer(
            renderer,
            NC_RENDERER_BUFFER_USAGE_INDEX,
            NC__NUKLEAR_INITIAL_BUFFER_CAPACITY);
    NC_CHECK_RESULT(backend->index_gpu_buffer, "Failed to create the Nuklear index buffer.");

    return backend;

error:
    nc_nuklear_backend_fini(backend, renderer);
    return NULL;
}

bool nc_nuklear_backend_set_scale(
    nc_nuklear_backend_t* backend,
    nc_renderer_t* renderer,
    const float scale
) {
    return backend->font_texture_scale == scale ||
            nc__nuklear_rebuild_font(backend, renderer, scale);
}

struct nk_context* nc_nuklear_backend_get_context(nc_nuklear_backend_t* backend) {
    return &backend->context;
}

bool nc_nuklear_backend_handle_event(
    nc_nuklear_backend_t* backend,
    const nc_nuklear_view_t* view,
    const SDL_Event* event,
    const bool mouse_input_enabled
) {
    if (nc__nuklear_is_synthetic_touch_mouse_event(event)) {
        return true;
    }
    if (!mouse_input_enabled && nc__nuklear_is_mouse_event(event)) {
        nc__nuklear_release_mouse(backend, true);
        return false;
    }

    switch (event->type) {
        case SDL_EVENT_FINGER_DOWN: {
            const vkm_vec2 position = nc__nuklear_normalized_to_gui_position(
                    view,
                    event->tfinger.x,
                    event->tfinger.y);
            if (backend->touch_active ||
                    !nc__nuklear_point_in_interactive_window(backend, position.x, position.y)) {
                return false;
            }

            backend->touch_active = true;
            backend->touch_finger = event->tfinger.fingerID;
            nc__nuklear_motion(backend, position);
            nk_input_button(
                    &backend->context,
                    NK_BUTTON_LEFT,
                    (int)lroundf(position.x),
                    (int)lroundf(position.y),
                    true);
            return true;
        }
        case SDL_EVENT_FINGER_MOTION:
            if (!backend->touch_active || backend->touch_finger != event->tfinger.fingerID) {
                return false;
            }
            nc__nuklear_motion(
                    backend,
                    nc__nuklear_normalized_to_gui_position(
                            view,
                            event->tfinger.x,
                            event->tfinger.y));
            return true;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (!backend->touch_active || backend->touch_finger != event->tfinger.fingerID) {
                return false;
            }

            const vkm_vec2 position = nc__nuklear_normalized_to_gui_position(
                    view,
                    event->tfinger.x,
                    event->tfinger.y);
            nc__nuklear_motion(backend, position);
            nk_input_button(
                    &backend->context,
                    NK_BUTTON_LEFT,
                    (int)lroundf(position.x),
                    (int)lroundf(position.y),
                    false);
            backend->touch_active = false;
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            nc__nuklear_key(backend, &event->key);
            return backend->context.text_edit.active;
        case SDL_EVENT_MOUSE_MOTION: {
            const vkm_vec2 position = nc__nuklear_window_to_gui_position(
                    view,
                    event->motion.x,
                    event->motion.y);
            nc__nuklear_motion(backend, position);
            return backend->mouse_captured ||
                    nc__nuklear_point_in_interactive_window(backend, position.x, position.y);
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const vkm_vec2 position = nc__nuklear_window_to_gui_position(
                    view,
                    event->button.x,
                    event->button.y);
            nc__nuklear_motion(backend, position);
            const bool hovered =
                    nc__nuklear_point_in_interactive_window(backend, position.x, position.y);
            const bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            const enum nk_buttons button = nc__nuklear_mouse_button(event->button.button);
            if (button != NK_BUTTON_MAX) {
                nk_input_button(
                        &backend->context,
                        button,
                        (int)lroundf(position.x),
                        (int)lroundf(position.y),
                        down);
                if (event->button.button == SDL_BUTTON_LEFT && (!down || event->button.clicks >= 2)) {
                    nk_input_button(
                            &backend->context,
                            NK_BUTTON_DOUBLE,
                            (int)lroundf(position.x),
                            (int)lroundf(position.y),
                            down);
                }
            }

            const bool captured = backend->mouse_captured || hovered;
            if (down && hovered) {
                backend->mouse_captured = true;
            } else if (!down) {
                backend->mouse_captured = false;
            }
            return captured;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            const vkm_vec2 position = nc__nuklear_window_to_gui_position(
                    view,
                    event->wheel.mouse_x,
                    event->wheel.mouse_y);
            nc__nuklear_motion(backend, position);
            nk_input_scroll(&backend->context, nk_vec2(event->wheel.x, event->wheel.y));
            return backend->mouse_captured ||
                    nc__nuklear_point_in_interactive_window(backend, position.x, position.y);
        }
        case SDL_EVENT_TEXT_INPUT: {
            const char* text = event->text.text;
            while (*text) {
                nk_rune rune;
                const int length = nk_utf_decode(text, &rune, (int)strlen(text));
                if (length <= 0) {
                    break;
                }
                nk_input_unicode(&backend->context, rune);
                text += length;
            }
            return backend->context.text_edit.active;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            nc__nuklear_release_input(backend);
            return false;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            nk_input_motion(&backend->context, -1, -1);
            return backend->mouse_captured;
        default:
            return false;
    }
}

bool nc_nuklear_backend_is_keyboard_captured(const nc_nuklear_backend_t* backend) {
    return backend->context.text_edit.active;
}

void nc_nuklear_backend_begin_frame(nc_nuklear_backend_t* backend, const float delta_time) {
    nk_input_end(&backend->context);
    backend->context.delta_time_seconds = delta_time;
    backend->draw_command_count = 0;
    nk_buffer_clear(&backend->command_buffer);
    nk_buffer_clear(&backend->vertex_buffer);
    nk_buffer_clear(&backend->index_buffer);
}

bool nc_nuklear_backend_end_frame(
    nc_nuklear_backend_t* backend,
    nc_renderer_t* renderer,
    const nc_nuklear_view_t* view
) {
    const nk_flags convert_result = nk_convert(
            &backend->context,
            &backend->command_buffer,
            &backend->vertex_buffer,
            &backend->index_buffer,
            &(struct nk_convert_config){
                .global_alpha = 1.0f,
                .line_AA = NK_ANTI_ALIASING_ON,
                .shape_AA = NK_ANTI_ALIASING_ON,
                .circle_segment_count = 22,
                .arc_segment_count = 22,
                .curve_segment_count = 22,
                .tex_null = backend->null_texture,
                .vertex_layout = nc__nuklear_vertex_layout,
                .vertex_size = sizeof(nc__nuklear_vertex_t),
                .vertex_alignment = _Alignof(nc__nuklear_vertex_t),
            });
    if (convert_result != NK_CONVERT_SUCCESS) {
        NC_SET_ERROR("nk_convert() failed with code %u", convert_result);
        goto error;
    }

    uint32_t first_index = 0;
    const struct nk_draw_command* draw_command;
    nk_draw_foreach(draw_command, &backend->context, &backend->command_buffer) {
        NC_CHECK_RESULT(
                draw_command->elem_count <= UINT32_MAX - first_index,
                "The Nuklear index buffer is too large.");
        if (draw_command->elem_count == 0 || !draw_command->texture.ptr) {
            first_index += draw_command->elem_count;
            continue;
        }

        const float physical_clip_x = floorf(draw_command->clip_rect.x * view->scale);
        const float physical_clip_y = floorf(draw_command->clip_rect.y * view->scale);
        const float physical_clip_right =
                ceilf((draw_command->clip_rect.x + draw_command->clip_rect.w) * view->scale);
        const float physical_clip_bottom =
                ceilf((draw_command->clip_rect.y + draw_command->clip_rect.h) * view->scale);
        const int clip_x = (int)vkm_clamp(physical_clip_x, 0.0f, (float)view->pixel_viewport.x);
        const int clip_y = (int)vkm_clamp(physical_clip_y, 0.0f, (float)view->pixel_viewport.y);
        const int clip_right =
                (int)vkm_clamp(physical_clip_right, 0.0f, (float)view->pixel_viewport.x);
        const int clip_bottom =
                (int)vkm_clamp(physical_clip_bottom, 0.0f, (float)view->pixel_viewport.y);

        if (clip_right > clip_x && clip_bottom > clip_y) {
            nc__nuklear_reserve_draw_commands(backend, 1);
            backend->draw_commands[backend->draw_command_count++] =
                    (nc_renderer_overlay_draw_command_t){
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

    if (backend->draw_command_count > 0) {
        NC_CHECK_RESULT(
                nk_buffer_total(&backend->vertex_buffer) <= UINT32_MAX &&
                nk_buffer_total(&backend->index_buffer) <= UINT32_MAX,
                "The converted Nuklear buffers are too large.");
        const uint32_t vertex_bytes = (uint32_t)nk_buffer_total(&backend->vertex_buffer);
        const uint32_t index_bytes = (uint32_t)nk_buffer_total(&backend->index_buffer);
        if (!nc_renderer_queue_buffer_upload(
                renderer,
                backend->vertex_gpu_buffer,
                nk_buffer_memory_const(&backend->vertex_buffer),
                vertex_bytes)) {
            goto error;
        }
        if (!nc_renderer_queue_buffer_upload(
                renderer,
                backend->index_gpu_buffer,
                nk_buffer_memory_const(&backend->index_buffer),
                index_bytes)) {
            goto error;
        }
    }

    const bool text_input_enabled = backend->context.text_edit.active;
    if (backend->text_input_enabled != text_input_enabled) {
        if (!nc_renderer_set_text_input_enabled(renderer, text_input_enabled)) {
            goto error;
        }
        backend->text_input_enabled = text_input_enabled;
    }

    nc__nuklear_finish_frame(backend);
    return true;

error:
    backend->draw_command_count = 0;
    nc__nuklear_finish_frame(backend);
    return false;
}

void nc_nuklear_backend_get_draw(
    const nc_nuklear_backend_t* backend,
    const nc_nuklear_view_t* view,
    nc_renderer_overlay_draw_t* draw
) {
    *draw = (nc_renderer_overlay_draw_t){
        .vertex_buffer = backend->vertex_gpu_buffer,
        .index_buffer = backend->index_gpu_buffer,
        .draw_commands = backend->draw_commands,
        .draw_command_count = backend->draw_command_count,
        .scale = view->scale,
    };
}

void nc_nuklear_backend_fini(nc_nuklear_backend_t* backend, nc_renderer_t* renderer) {
    if (!backend) {
        return;
    }

    if (backend->text_input_enabled) {
        nc_renderer_set_text_input_enabled(renderer, false);
    }
    nc_renderer_destroy_buffer(renderer, backend->index_gpu_buffer);
    nc_renderer_destroy_buffer(renderer, backend->vertex_gpu_buffer);
    nc_renderer_destroy_texture(renderer, backend->font_texture);
    free(backend->draw_commands);

    if (backend->context_initialized) {
        nk_free(&backend->context);
    }
    if (backend->font_atlas_initialized) {
        nk_font_atlas_clear(&backend->font_atlas);
    }
    nk_buffer_free(&backend->index_buffer);
    nk_buffer_free(&backend->vertex_buffer);
    nk_buffer_free(&backend->command_buffer);
    free(backend);
}
