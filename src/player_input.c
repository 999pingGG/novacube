#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <novacube/cvar.h>
#include <novacube/cvkm.h>
#include <novacube/entity_command.h>
#include <novacube/macros.h>
#include <novacube/player_input.h>
#include <novacube/renderer.h>

#define NC__PLAYER_INPUT_MAX_TOUCHES 8
#define NC__PLAYER_INPUT_MODE_SWITCH_THRESHOLD 0.2f
#define NC__PLAYER_INPUT_MAX_LOGICAL_SIZE 32767.0f

typedef uint8_t nc__player_input_touch_action_t;
enum {
    NC__PLAYER_INPUT_TOUCH_ACTION_MOVE_ANALOG_STICK,
    NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_ANALOG_STICK,
    NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_FREE_DRAG,

    NC__PLAYER_INPUT_TOUCH_ACTION_COUNT,
    NC__PLAYER_INPUT_TOUCH_ACTION_NONE = NC__PLAYER_INPUT_TOUCH_ACTION_COUNT,
};

typedef struct nc__player_input_pointer_t {
    SDL_FingerID finger_id;
    vkm_vec2 position;
    vkm_vec2 initial_position;
    nc_player_input_control_id_t captured_control;
    nc__player_input_touch_action_t touch_action;
    bool active;
} nc__player_input_pointer_t;

typedef struct nc_player_input_t {
    vkm_usvec2 window_size;
    vkm_usvec2 pixel_viewport;
    float gui_scale;
    SDL_FRect safe_area;
    SDL_FRect control_rects[NC_PLAYER_INPUT_CONTROL_COUNT];

    nc__player_input_pointer_t touch_points[NC__PLAYER_INPUT_MAX_TOUCHES];
    nc_entity_actions_t pending_actions;
    vkm_vec2 look_delta;
    float mode_switch_accumulator;
    bool touch_controls_enabled;
} nc_player_input_t;

static float nc__player_input_nonnegative_size(const double value) {
    if (!isfinite(value) || value <= 0.0) {
        return 0.0f;
    }

    return (float)vkm_min(value, (double)NC__PLAYER_INPUT_MAX_LOGICAL_SIZE);
}

static vkm_vec2 nc__player_input_window_to_gui_position(
    const nc_player_input_t* input,
    const float window_x,
    const float window_y
) {
    if (input->window_size.x == 0 || input->window_size.y == 0 || input->gui_scale <= 0.0f) {
        return (vkm_vec2){ { 0.0f, 0.0f } };
    }

    return (vkm_vec2){ {
        window_x * (float)input->pixel_viewport.x / (float)input->window_size.x / input->gui_scale,
        window_y * (float)input->pixel_viewport.y / (float)input->window_size.y / input->gui_scale,
    } };
}

static vkm_vec2 nc__player_input_normalized_to_gui_position(
    const nc_player_input_t* input,
    const float normalized_x,
    const float normalized_y
) {
    if (input->gui_scale <= 0.0f) {
        return (vkm_vec2){ { 0.0f, 0.0f } };
    }

    return (vkm_vec2){ {
        normalized_x * (float)input->pixel_viewport.x / input->gui_scale,
        normalized_y * (float)input->pixel_viewport.y / input->gui_scale,
    } };
}

static bool nc__player_input_point_in_rect(const SDL_FRect* rect, const float x, const float y) {
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

static bool nc__player_input_is_planar_movement_control(const nc_player_input_control_id_t control_id) {
    return     control_id == NC_PLAYER_INPUT_CONTROL_LEFT
            || control_id == NC_PLAYER_INPUT_CONTROL_RIGHT
            || control_id == NC_PLAYER_INPUT_CONTROL_FORWARD
            || control_id == NC_PLAYER_INPUT_CONTROL_BACKWARD;
}

static bool nc__player_input_uses_movement_buttons(void) {
    return nc_cvar_get_touch_movement_mode() == NC_TOUCH_MOVEMENT_MODE_BUTTONS;
}

static bool nc__player_input_uses_camera_analog_stick(void) {
    return nc_cvar_get_touch_camera_mode() == NC_TOUCH_CAMERA_MODE_ANALOG;
}

static bool nc__player_input_is_control_visible(const nc_player_input_control_id_t control_id) {
    return nc__player_input_uses_movement_buttons() ||
            !nc__player_input_is_planar_movement_control(control_id);
}

static float nc__player_input_analog_stick_ring_radius(void) {
    return nc__player_input_nonnegative_size(nc_cvar_get_analog_stick_ring_radius());
}

static void nc__player_input_layout_controls(nc_player_input_t* input) {
    const float safe_left = input->safe_area.x;
    const float safe_top = input->safe_area.y;
    const float safe_right = input->safe_area.x + input->safe_area.w;
    const float safe_bottom = input->safe_area.y + input->safe_area.h;
    const float button_size = nc__player_input_nonnegative_size(nc_cvar_get_gui_button_size());
    const float gap = button_size * 0.12f;
    const float margin = button_size * 0.35f;
    const float dpad_width = button_size * 3.0f + gap * 2.0f;
    const float dpad_height = dpad_width;
    const float action_row_width = button_size * 2.0f + gap;
    const float right_column_height = button_size * 2.0f + gap;
    const float action_row_bottom_gap = button_size * 0.45f + gap * 2.0f;

    const float dpad_left = vkm_min(
            vkm_max(safe_left + margin, safe_left),
            vkm_max(safe_right - margin - dpad_width, safe_left));
    const float dpad_top = vkm_min(
            vkm_max(safe_bottom - margin - dpad_height, safe_top),
            vkm_max(safe_bottom - button_size, safe_top));

    input->control_rects[NC_PLAYER_INPUT_CONTROL_FORWARD] = (SDL_FRect){
        .x = dpad_left + button_size + gap,
        .y = dpad_top,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_LEFT] = (SDL_FRect){
        .x = dpad_left,
        .y = dpad_top + button_size + gap,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_BACKWARD] = (SDL_FRect){
        .x = dpad_left + button_size + gap,
        .y = dpad_top + (button_size + gap) * 2.0f,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_RIGHT] = (SDL_FRect){
        .x = dpad_left + (button_size + gap) * 2.0f,
        .y = dpad_top + button_size + gap,
        .w = button_size,
        .h = button_size,
    };

    const float right_column_left = vkm_max(safe_right - margin - button_size, safe_left);
    const float right_column_top = vkm_min(
            vkm_max(safe_bottom - margin - right_column_height, safe_top),
            vkm_max(safe_bottom - button_size, safe_top));
    const float action_row_left = vkm_max(safe_right - margin - action_row_width, safe_left);
    const float action_row_top = vkm_max(
            right_column_top - button_size - action_row_bottom_gap,
            safe_top);

    input->control_rects[NC_PLAYER_INPUT_CONTROL_PLACE_BLOCK] = (SDL_FRect){
        .x = action_row_left,
        .y = action_row_top,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_REMOVE_BLOCK] = (SDL_FRect){
        .x = action_row_left + button_size + gap,
        .y = action_row_top,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_UP] = (SDL_FRect){
        .x = right_column_left,
        .y = right_column_top,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_DOWN] = (SDL_FRect){
        .x = right_column_left,
        .y = right_column_top + button_size + gap,
        .w = button_size,
        .h = button_size,
    };
}

static nc__player_input_pointer_t* nc__player_input_find_touch(
    nc_player_input_t* input,
    const SDL_FingerID finger_id
) {
    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        if (input->touch_points[i].active && input->touch_points[i].finger_id == finger_id) {
            return &input->touch_points[i];
        }
    }

    return NULL;
}

static nc__player_input_pointer_t* nc__player_input_alloc_touch(nc_player_input_t* input) {
    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        if (!input->touch_points[i].active) {
            return &input->touch_points[i];
        }
    }

    return NULL;
}

static vkm_vec2 nc__player_input_pointer_position(
    const nc_player_input_t* input,
    const nc__player_input_pointer_t* pointer
) {
    return nc__player_input_normalized_to_gui_position(input, pointer->position.x, pointer->position.y);
}

static vkm_vec2 nc__player_input_pointer_start_position(
    const nc_player_input_t* input,
    const nc__player_input_pointer_t* pointer
) {
    return nc__player_input_normalized_to_gui_position(
            input,
            pointer->initial_position.x,
            pointer->initial_position.y);
}

static vkm_vec2 nc__player_input_clamped_stick_position(
    const nc_player_input_t* input,
    const nc__player_input_pointer_t* pointer
) {
    const vkm_vec2 ring_position = nc__player_input_pointer_start_position(input, pointer);
    const vkm_vec2 pointer_position = nc__player_input_pointer_position(input, pointer);
    vkm_vec2 delta;
    vkm_sub(&pointer_position, &ring_position, &delta);

    const float radius = nc__player_input_analog_stick_ring_radius();
    const float length = vkm_length(&delta);
    if (length > radius && length > 0.0f) {
        vkm_mul(&delta, radius / length, &delta);
    }

    vkm_vec2 result;
    vkm_add(&ring_position, &delta, &result);
    return result;
}

static bool nc__player_input_get_stick_delta(
    const nc_player_input_t* input,
    const nc__player_input_touch_action_t touch_action,
    vkm_vec2* delta
) {
    *delta = (vkm_vec2){ { 0.0f, 0.0f } };

    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        const nc__player_input_pointer_t* pointer = &input->touch_points[i];
        if (!pointer->active || pointer->touch_action != touch_action) {
            continue;
        }

        const vkm_vec2 ring_position = nc__player_input_pointer_start_position(input, pointer);
        const vkm_vec2 stick_position = nc__player_input_clamped_stick_position(input, pointer);
        vkm_sub(&stick_position, &ring_position, delta);

        const float radius = nc__player_input_analog_stick_ring_radius();
        if (radius > 0.0f) {
            delta->x /= radius;
            delta->y /= -radius;
        }
        return true;
    }

    return false;
}

static bool nc__player_input_touch_action_captured(
    const nc_player_input_t* input,
    const nc__player_input_touch_action_t touch_action
) {
    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        if (input->touch_points[i].active && input->touch_points[i].touch_action == touch_action) {
            return true;
        }
    }

    return false;
}

static nc__player_input_touch_action_t nc__player_input_touch_action_for_touch(
    const nc_player_input_t* input,
    const float x
) {
    nc__player_input_touch_action_t action = nc__player_input_uses_camera_analog_stick()
            ? NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_ANALOG_STICK
            : NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_FREE_DRAG;

    const float gui_viewport_width = input->gui_scale > 0.0f
            ? (float)input->pixel_viewport.x / input->gui_scale
            : 0.0f;
    if (!nc__player_input_uses_movement_buttons() && x < gui_viewport_width * 0.5f) {
        action = NC__PLAYER_INPUT_TOUCH_ACTION_MOVE_ANALOG_STICK;
    }

    return nc__player_input_touch_action_captured(input, action)
            ? NC__PLAYER_INPUT_TOUCH_ACTION_NONE
            : action;
}

static nc_player_input_control_id_t nc__player_input_hit_test(
    const nc_player_input_t* input,
    const float x,
    const float y
) {
    for (uint8_t i = 0; i < (uint8_t)NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        if (nc__player_input_is_control_visible(i) &&
                nc__player_input_point_in_rect(&input->control_rects[i], x, y)) {
            return i;
        }
    }

    return NC_PLAYER_INPUT_CONTROL_COUNT;
}

static bool nc__player_input_control_pressed(
    const nc_player_input_t* input,
    const nc_player_input_control_id_t control_id
) {
    if (!nc__player_input_is_control_visible(control_id)) {
        return false;
    }

    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        const nc__player_input_pointer_t* pointer = &input->touch_points[i];
        if (!pointer->active || pointer->captured_control != control_id) {
            continue;
        }

        const vkm_vec2 position = nc__player_input_pointer_position(input, pointer);
        if (nc__player_input_point_in_rect(&input->control_rects[control_id], position.x, position.y)) {
            return true;
        }
    }

    return false;
}

static void nc__player_input_clear_touches(nc_player_input_t* input) {
    memset(input->touch_points, 0, sizeof(input->touch_points));
}

static void nc__player_input_disable_touch_controls(nc_player_input_t* input) {
    nc__player_input_clear_touches(input);
    input->touch_controls_enabled = false;
    input->mode_switch_accumulator = 0.0f;
}

static bool nc__player_input_is_synthetic_touch_mouse_event(const SDL_Event* event) {
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

static bool nc__player_input_touchscreen_available(void) {
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

nc_player_input_t* nc_player_input_init(nc_renderer_t* renderer, const float gui_scale) {
    nc_player_input_t* input = calloc(1, sizeof(*input));
    input->touch_controls_enabled = nc__player_input_touchscreen_available();
    nc_player_input_update_view(input, renderer, gui_scale);
    return input;
}

void nc_player_input_update_view(
    nc_player_input_t* input,
    const nc_renderer_t* renderer,
    const float gui_scale
) {
    input->window_size = nc_renderer_get_window_size(renderer);
    input->pixel_viewport = nc_renderer_get_viewport(renderer);
    input->gui_scale = gui_scale;

    SDL_Rect window_safe_area;
    nc_renderer_get_window_safe_area(renderer, &window_safe_area);
    const vkm_vec2 top_left = nc__player_input_window_to_gui_position(
            input,
            (float)window_safe_area.x,
            (float)window_safe_area.y);
    const vkm_vec2 bottom_right = nc__player_input_window_to_gui_position(
            input,
            (float)(window_safe_area.x + window_safe_area.w),
            (float)(window_safe_area.y + window_safe_area.h));
    input->safe_area = (SDL_FRect){
        .x = top_left.x,
        .y = top_left.y,
        .w = vkm_max(bottom_right.x - top_left.x, 0.0f),
        .h = vkm_max(bottom_right.y - top_left.y, 0.0f),
    };

    // There are only eight rectangles. Rebuilding them avoids stale cvar-dependent layout state.
    nc__player_input_layout_controls(input);
    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        nc__player_input_pointer_t* pointer = &input->touch_points[i];
        if (pointer->active &&
                pointer->captured_control < NC_PLAYER_INPUT_CONTROL_COUNT &&
                !nc__player_input_is_control_visible(pointer->captured_control)) {
            *pointer = (nc__player_input_pointer_t){ 0 };
        }
    }
}

void nc_player_input_update(nc_player_input_t* input, const float delta_time) {
    // Keep the existing automatic input-mode heuristic. Looking around accumulates intent in the event handler,
    // while elapsed time decays it here.
    input->mode_switch_accumulator -= delta_time;
    if (input->mode_switch_accumulator > NC__PLAYER_INPUT_MODE_SWITCH_THRESHOLD) {
        input->touch_controls_enabled = !input->touch_controls_enabled;
        input->mode_switch_accumulator = 0.0f;
        if (!input->touch_controls_enabled) {
            nc__player_input_clear_touches(input);
        }
    } else if (input->mode_switch_accumulator < 0.0f) {
        input->mode_switch_accumulator = 0.0f;
    }
}

bool nc_player_input_handle_event(
    nc_player_input_t* input,
    nc_renderer_t* renderer,
    const SDL_Event* event,
    const bool gui_captured
) {
    if (nc__player_input_is_synthetic_touch_mouse_event(event)) {
        return true;
    }

    switch (event->type) {
        case SDL_EVENT_FINGER_DOWN: {
            if (gui_captured || !input->touch_controls_enabled) {
                return true;
            }

            const vkm_vec2 position = nc__player_input_normalized_to_gui_position(
                    input,
                    event->tfinger.x,
                    event->tfinger.y);
            const nc_player_input_control_id_t control_id =
                    nc__player_input_hit_test(input, position.x, position.y);
            const nc__player_input_touch_action_t touch_action =
                    control_id == NC_PLAYER_INPUT_CONTROL_COUNT
                    ? nc__player_input_touch_action_for_touch(input, position.x)
                    : NC__PLAYER_INPUT_TOUCH_ACTION_NONE;
            if (control_id == NC_PLAYER_INPUT_CONTROL_COUNT &&
                    touch_action == NC__PLAYER_INPUT_TOUCH_ACTION_NONE) {
                return true;
            }

            nc__player_input_pointer_t* pointer = nc__player_input_alloc_touch(input);
            if (pointer) {
                *pointer = (nc__player_input_pointer_t){
                    .finger_id = event->tfinger.fingerID,
                    .position = { { event->tfinger.x, event->tfinger.y } },
                    .initial_position = { { event->tfinger.x, event->tfinger.y } },
                    .captured_control = control_id,
                    .touch_action = touch_action,
                    .active = true,
                };
            }
            return true;
        }
        case SDL_EVENT_FINGER_MOTION: {
            if (gui_captured) {
                return true;
            }
            if (!input->touch_controls_enabled) {
                const vkm_vec2 delta = { { event->tfinger.dx, event->tfinger.dy } };
                // Preserve the existing heuristic's deliberately higher touch sensitivity.
                input->mode_switch_accumulator += vkm_length(&delta) * 2.0f;
                return true;
            }

            nc__player_input_pointer_t* pointer =
                    nc__player_input_find_touch(input, event->tfinger.fingerID);
            if (!pointer) {
                return true;
            }

            pointer->position = (vkm_vec2){ { event->tfinger.x, event->tfinger.y } };
            if (pointer->touch_action == NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_FREE_DRAG) {
                const float sensitivity = (float)nc_cvar_get_touch_camera_drag_sensitivity();
                input->look_delta.x += event->tfinger.dx * (float)input->pixel_viewport.x * sensitivity;
                input->look_delta.y -= event->tfinger.dy * (float)input->pixel_viewport.y * sensitivity;
            }
            return true;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (gui_captured) {
                return true;
            }

            nc__player_input_pointer_t* pointer =
                    nc__player_input_find_touch(input, event->tfinger.fingerID);
            if (!pointer) {
                return true;
            }

            pointer->position = (vkm_vec2){ { event->tfinger.x, event->tfinger.y } };
            const vkm_vec2 position = nc__player_input_pointer_position(input, pointer);
            if (event->type == SDL_EVENT_FINGER_UP &&
                    pointer->captured_control < NC_PLAYER_INPUT_CONTROL_COUNT &&
                    nc__player_input_is_control_visible(pointer->captured_control) &&
                    nc__player_input_point_in_rect(
                            &input->control_rects[pointer->captured_control],
                            position.x,
                            position.y)) {
                if (pointer->captured_control == NC_PLAYER_INPUT_CONTROL_PLACE_BLOCK) {
                    input->pending_actions |= NC_ENTITY_ACTION_PLACE_BLOCK;
                } else if (pointer->captured_control == NC_PLAYER_INPUT_CONTROL_REMOVE_BLOCK) {
                    input->pending_actions |= NC_ENTITY_ACTION_REMOVE_BLOCK;
                }
            }

            *pointer = (nc__player_input_pointer_t){ 0 };
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            nc__player_input_disable_touch_controls(input);
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            if (input->touch_controls_enabled) {
                const vkm_vec2 delta = { { event->motion.xrel, event->motion.yrel } };
                // Preserve the existing heuristic's deliberately lower mouse sensitivity.
                input->mode_switch_accumulator += vkm_length(&delta) * 0.002f;
            }
            if (!gui_captured && nc_renderer_is_relative_mouse_mode(renderer)) {
                const float sensitivity = vkm_deg2rad((float)nc_cvar_get_mouse_sensitivity());
                input->look_delta.x += event->motion.xrel * sensitivity;
                input->look_delta.y -= event->motion.yrel * sensitivity;
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            nc__player_input_disable_touch_controls(input);
            if (gui_captured || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                return true;
            }
            if (!nc_renderer_set_relative_mouse_mode(renderer, true)) {
                return false;
            }
            if (event->button.button == SDL_BUTTON_RIGHT) {
                input->pending_actions |= NC_ENTITY_ACTION_PLACE_BLOCK;
            } else if (event->button.button == SDL_BUTTON_LEFT) {
                input->pending_actions |= NC_ENTITY_ACTION_REMOVE_BLOCK;
            }
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            nc__player_input_disable_touch_controls(input);
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            nc__player_input_clear_touches(input);
            input->look_delta = (vkm_vec2){ { 0.0f, 0.0f } };
            return true;
        default:
            return true;
    }
}

void nc_player_input_get_entity_command(
    nc_player_input_t* input,
    const bool keyboard_captured,
    const float delta_time,
    nc_entity_command_t* command
) {
    const bool* keyboard = SDL_GetKeyboardState(NULL);
    *command = (nc_entity_command_t){
        .movement = { {
            keyboard_captured
                    ? 0.0f
                    : (float)keyboard[SDL_SCANCODE_D] - (float)keyboard[SDL_SCANCODE_A],
            keyboard_captured
                    ? 0.0f
                    : (float)keyboard[SDL_SCANCODE_SPACE] - (float)keyboard[SDL_SCANCODE_LSHIFT],
            keyboard_captured
                    ? 0.0f
                    : (float)keyboard[SDL_SCANCODE_W] - (float)keyboard[SDL_SCANCODE_S],
        } },
        .look_delta = input->look_delta,
        .actions = input->pending_actions,
        .sprint = !keyboard_captured && keyboard[SDL_SCANCODE_LCTRL],
    };

    command->movement.x +=
            (float)nc__player_input_control_pressed(input, NC_PLAYER_INPUT_CONTROL_RIGHT) -
            (float)nc__player_input_control_pressed(input, NC_PLAYER_INPUT_CONTROL_LEFT);
    command->movement.y +=
            (float)nc__player_input_control_pressed(input, NC_PLAYER_INPUT_CONTROL_UP) -
            (float)nc__player_input_control_pressed(input, NC_PLAYER_INPUT_CONTROL_DOWN);
    command->movement.z +=
            (float)nc__player_input_control_pressed(input, NC_PLAYER_INPUT_CONTROL_FORWARD) -
            (float)nc__player_input_control_pressed(input, NC_PLAYER_INPUT_CONTROL_BACKWARD);

    vkm_vec2 movement_stick;
    nc__player_input_get_stick_delta(
            input,
            NC__PLAYER_INPUT_TOUCH_ACTION_MOVE_ANALOG_STICK,
            &movement_stick);
    command->movement.x += movement_stick.x;
    command->movement.z += movement_stick.y;

    vkm_vec2 camera_stick;
    if (nc__player_input_get_stick_delta(
            input,
            NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_ANALOG_STICK,
            &camera_stick)) {
        const float sensitivity = (float)nc_cvar_get_touch_camera_stick_sensitivity();
        command->look_delta.x += camera_stick.x * sensitivity * delta_time;
        command->look_delta.y += camera_stick.y * sensitivity * delta_time;
    }

    const float movement_length = vkm_length(&command->movement);
    if (movement_length > 1.0f) {
        vkm_div(&command->movement, movement_length, &command->movement);
    }

    input->look_delta = (vkm_vec2){ { 0.0f, 0.0f } };
    input->pending_actions = 0;
}

void nc_player_input_get_overlay(const nc_player_input_t* input, nc_player_input_overlay_t* overlay) {
    *overlay = (nc_player_input_overlay_t){
        .analog_stick_ring_radius = nc__player_input_analog_stick_ring_radius(),
        .analog_stick_ring_thickness = vkm_min(
                nc__player_input_nonnegative_size(nc_cvar_get_analog_stick_ring_thickness()),
                nc__player_input_analog_stick_ring_radius()),
        .analog_stick_radius = vkm_min(
                nc__player_input_nonnegative_size(nc_cvar_get_analog_stick_radius()),
                nc__player_input_analog_stick_ring_radius()),
        .touch_controls_enabled = input->touch_controls_enabled,
    };

    for (uint8_t i = 0; i < (uint8_t)NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        overlay->control_rects[i] = input->control_rects[i];
        overlay->controls_visible[i] = nc__player_input_is_control_visible(i);
        overlay->controls_pressed[i] = nc__player_input_control_pressed(input, i);
    }

    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        const nc__player_input_pointer_t* pointer = &input->touch_points[i];
        if (!pointer->active) {
            continue;
        }

        uint8_t analog_stick;
        if (pointer->touch_action == NC__PLAYER_INPUT_TOUCH_ACTION_MOVE_ANALOG_STICK) {
            analog_stick = 0;
        } else if (pointer->touch_action == NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_ANALOG_STICK) {
            analog_stick = 1;
        } else {
            continue;
        }

        overlay->analog_sticks_active[analog_stick] = true;
        overlay->analog_stick_ring_positions[analog_stick] =
                nc__player_input_pointer_start_position(input, pointer);
        overlay->analog_stick_positions[analog_stick] =
                nc__player_input_clamped_stick_position(input, pointer);
    }
}

void nc_player_input_fini(nc_player_input_t* input) {
    free(input);
}
