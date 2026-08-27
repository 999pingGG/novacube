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

// A touch not captured by a button owns at most one continuous-control role.
typedef uint8_t nc__player_input_touch_action_t;
enum {
    NC__PLAYER_INPUT_TOUCH_ACTION_MOVE_ANALOG_STICK,
    NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_ANALOG_STICK,
    NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_FREE_DRAG,

    NC__PLAYER_INPUT_TOUCH_ACTION_COUNT,
    NC__PLAYER_INPUT_TOUCH_ACTION_NONE = NC__PLAYER_INPUT_TOUCH_ACTION_COUNT,
};

// SDL stores touch positions normalized to the window. Keeping both the current and initial
// position lets an analog stick remain anchored where the finger first touched.
typedef struct nc__player_input_pointer_t {
    SDL_FingerID finger_id;
    vkm_vec2 position;
    vkm_vec2 initial_position;
    nc_player_input_control_id_t captured_control;
    nc__player_input_touch_action_t touch_action;
    bool active;
} nc__player_input_pointer_t;

typedef struct nc_player_input_t {
    nc_gui_view_t view;
    SDL_FRect control_rects[NC_PLAYER_INPUT_CONTROL_COUNT];

    nc__player_input_pointer_t touch_points[NC__PLAYER_INPUT_MAX_TOUCHES];
    nc_entity_actions_t pending_actions;
    vkm_vec2 look_delta;
    float mode_switch_accumulator;
    // Previous modes are retained only to cancel gestures whose meaning changed between frames.
    int previous_touch_movement_mode;
    int previous_touch_camera_mode;
    bool touch_controls_enabled;
} nc_player_input_t;

// Converts SDL's normalized touch coordinates into the same logical units as the controls.
static vkm_vec2 nc__player_input_normalized_to_gui_position(
    const nc_player_input_t* input,
    const float normalized_x,
    const float normalized_y
) {
    return (vkm_vec2){ {
            normalized_x * (float)input->view.framebuffer_size.x,
            normalized_y * (float)input->view.framebuffer_size.y,
    } };
}

// Gameplay controls are pass-through Clay elements, so player input owns their hit-testing.
static bool nc__player_input_point_in_rect(const SDL_FRect* rect, const float x, const float y) {
    return     x >= rect->x && x < rect->x + rect->w
            && y >= rect->y && y < rect->y + rect->h;
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
    // Button mode shows the D-pad plus vertical/action buttons. Analog movement replaces only
    // the four planar D-pad buttons.
    return      nc__player_input_uses_movement_buttons()
            || !nc__player_input_is_planar_movement_control(control_id);
}

static float nc__player_input_analog_stick_ring_radius(void) {
    return (float)nc_cvar_get_analog_stick_ring_radius();
}

// Builds the eight rectangles used by both HUD drawing and touch hit-testing.
static void nc__player_input_layout_controls(nc_player_input_t* input) {
    const float safe_left = input->view.gui_safe_area.x;
    const float safe_top = input->view.gui_safe_area.y;
    const float safe_right = safe_left + input->view.gui_safe_area.w;
    const float safe_bottom = safe_top + input->view.gui_safe_area.h;
    const float button_size = (float)nc_cvar_get_gui_button_size();
    const float gap = button_size * 0.12f;
    const float margin = button_size * 0.35f;
    const float dpad_width = button_size * 3.0f + gap * 2.0f;
    const float action_row_width = button_size * 2.0f + gap;
    const float right_column_height = button_size * 2.0f + gap;
    const float action_row_bottom_gap = button_size * 0.45f + gap * 2.0f;

    const float dpad_left = vkm_min(
            vkm_max(safe_left + margin, safe_left),
            vkm_max(safe_right - margin - dpad_width, safe_left));
    const float dpad_top = vkm_min(
            vkm_max(safe_bottom - margin - dpad_width, safe_top),
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
        .y = dpad_top + (button_size + gap) * 2,
        .w = button_size,
        .h = button_size,
    };
    input->control_rects[NC_PLAYER_INPUT_CONTROL_RIGHT] = (SDL_FRect){
        .x = dpad_left + (button_size + gap) * 2,
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

// Finds the slot tracking an SDL finger.
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

// Returns the first free multitouch slot, or NULL after all eight are occupied.
static nc__player_input_pointer_t* nc__player_input_alloc_touch(nc_player_input_t* input) {
    for (size_t i = 0; i < NC__PLAYER_INPUT_MAX_TOUCHES; i++) {
        if (!input->touch_points[i].active) {
            return &input->touch_points[i];
        }
    }

    return NULL;
}

// Returns a pointer's current position in GUI coordinates.
static vkm_vec2 nc__player_input_pointer_position(
    const nc_player_input_t* input,
    const nc__player_input_pointer_t* pointer
) {
    return nc__player_input_normalized_to_gui_position(input, pointer->position.x, pointer->position.y);
}

// Returns the fixed center of a floating analog stick in GUI coordinates.
static vkm_vec2 nc__player_input_pointer_start_position(
    const nc_player_input_t* input,
    const nc__player_input_pointer_t* pointer
) {
    return nc__player_input_normalized_to_gui_position(
            input,
            pointer->initial_position.x,
            pointer->initial_position.y);
}

// Clamps the visual stick knob to its ring while preserving the original touch direction.
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

// Finds the touch owning an analog role and returns its normalized [-1, 1] displacement.
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

// Prevents two touches from controlling the same analog role simultaneously.
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

// Assigns free touches to movement on the left half and camera control on the right half.
static nc__player_input_touch_action_t nc__player_input_touch_action_for_touch(
    const nc_player_input_t* input,
    const float x
) {
    nc__player_input_touch_action_t action = nc__player_input_uses_camera_analog_stick()
            ? NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_ANALOG_STICK
            : NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_FREE_DRAG;

    const float gui_width = (float)input->view.framebuffer_size.x;
    if (!nc__player_input_uses_movement_buttons() && x < gui_width * 0.5f) {
        action = NC__PLAYER_INPUT_TOUCH_ACTION_MOVE_ANALOG_STICK;
    }

    return nc__player_input_touch_action_captured(input, action)
            ? NC__PLAYER_INPUT_TOUCH_ACTION_NONE
            : action;
}

// Finds the visible gameplay button under a logical position.
static nc_player_input_control_id_t nc__player_input_hit_test(
    const nc_player_input_t* input,
    const float x,
    const float y
) {
    for (uint8_t i = 0; i < (uint8_t)NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        if (       nc__player_input_is_control_visible(i)
                && nc__player_input_point_in_rect(&input->control_rects[i], x, y)) {
            return i;
        }
    }

    return NC_PLAYER_INPUT_CONTROL_NONE;
}

// A button remains pressed only while its owning touch stays inside its rectangle.
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

nc_player_input_t* nc_player_input_init(const nc_gui_view_t* view) {
    nc_player_input_t* input = calloc(1, sizeof(*input));
    input->touch_controls_enabled = nc__player_input_touchscreen_available();
    nc_player_input_update_view(input, view);
    return input;
}

void nc_player_input_update_view(nc_player_input_t* input, const nc_gui_view_t* view) {
    input->view = *view;

    const int movement_mode = nc_cvar_get_touch_movement_mode();
    const int camera_mode = nc_cvar_get_touch_camera_mode();
    if (input->previous_touch_movement_mode != movement_mode ||
            input->previous_touch_camera_mode != camera_mode) {
        nc__player_input_clear_touches(input);
        input->previous_touch_movement_mode = movement_mode;
        input->previous_touch_camera_mode = camera_mode;
    }

    // Layout cvars can change at runtime, so rebuild the inexpensive eight-rectangle layout.
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
    const float direction = input->mode_switch_accumulator < 0.0f ? -1.0f : 1.0f;
    input->mode_switch_accumulator -= direction * delta_time;
    if (input->mode_switch_accumulator > NC__PLAYER_INPUT_MODE_SWITCH_THRESHOLD) {
        input->touch_controls_enabled = true;
        input->mode_switch_accumulator = 0.0f;
    } else if (input->mode_switch_accumulator < -NC__PLAYER_INPUT_MODE_SWITCH_THRESHOLD) {
        nc__player_input_disable_touch_controls(input);
    } else if (direction * input->mode_switch_accumulator < 0.0f) {
        input->mode_switch_accumulator = 0.0f;
    }
}

bool nc_player_input_handle_event(
    nc_player_input_t* input,
    nc_renderer_t* renderer,
    const SDL_Event* event
) {
    switch (event->type) {
        case SDL_EVENT_FINGER_DOWN: {
            if (!input->touch_controls_enabled) {
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
            if (!input->touch_controls_enabled) {
                return true;
            }

            nc__player_input_pointer_t* pointer =
                    nc__player_input_find_touch(input, event->tfinger.fingerID);
            if (!pointer) {
                return true;
            }

            const vkm_vec2 delta = { { event->tfinger.dx, event->tfinger.dy } };
            input->mode_switch_accumulator += vkm_length(&delta) * 2.0f;
            pointer->position = (vkm_vec2){ { event->tfinger.x, event->tfinger.y } };
            if (pointer->touch_action == NC__PLAYER_INPUT_TOUCH_ACTION_CAMERA_FREE_DRAG) {
                const float sensitivity = (float)nc_cvar_get_touch_camera_drag_sensitivity();
                input->look_delta.x +=
                        event->tfinger.dx * (float)input->view.framebuffer_size.x * sensitivity;
                input->look_delta.y -=
                        event->tfinger.dy * (float)input->view.framebuffer_size.y * sensitivity;
            }
            return true;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            nc__player_input_pointer_t* pointer =
                    nc__player_input_find_touch(input, event->tfinger.fingerID);
            if (!pointer) {
                return true;
            }
            if (event->type == SDL_EVENT_FINGER_CANCELED) {
                *pointer = (nc__player_input_pointer_t){ 0 };
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
            if (event->key.scancode >= SDL_SCANCODE_MODE) {
                // Ignore multimedia and mobile scancodes. Notably, the back button in mobile devices.
                return true;
            }

            switch (event->key.scancode) {
                case SDL_SCANCODE_F1:
                    input->pending_actions |= NC_ENTITY_ACTION_TOGGLE_CLAY_DEBUG_MODE;
                    break;
                default:
                    break;
            }

            nc__player_input_disable_touch_controls(input);
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            if (input->touch_controls_enabled) {
                const vkm_vec2 delta = { { event->motion.xrel, event->motion.yrel } };
                input->mode_switch_accumulator -= vkm_length(&delta) * 0.002f;
            }
            if (nc_renderer_is_relative_mouse_mode(renderer)) {
                const float sensitivity = vkm_deg2rad((float)nc_cvar_get_mouse_sensitivity());
                input->look_delta.x += event->motion.xrel * sensitivity;
                input->look_delta.y -= event->motion.yrel * sensitivity;
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            nc__player_input_disable_touch_controls(input);
            if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
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
    const float delta_time,
    nc_entity_command_t* command
) {
    const bool* keyboard = SDL_GetKeyboardState(NULL);
    *command = (nc_entity_command_t){
        .movement = { {
            (float)keyboard[SDL_SCANCODE_D] - (float)keyboard[SDL_SCANCODE_A],
            (float)keyboard[SDL_SCANCODE_SPACE] - (float)keyboard[SDL_SCANCODE_LSHIFT],
            (float)keyboard[SDL_SCANCODE_W] - (float)keyboard[SDL_SCANCODE_S],
        } },
        .look_delta = input->look_delta,
        .actions = input->pending_actions,
        .sprint = keyboard[SDL_SCANCODE_LCTRL],
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
                (float)nc_cvar_get_analog_stick_ring_thickness(),
                nc__player_input_analog_stick_ring_radius()),
        .analog_stick_radius = vkm_min(
                (float)nc_cvar_get_analog_stick_radius(),
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
