#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <novacube/build_info.h>
#include <novacube/camera.h>
#include <novacube/configuration.h>
#include <novacube/cvkm.h>
#include <novacube/error_handling.h>
#include <novacube/gui.h>
#include <novacube/macros.h>
#include <novacube/renderer.h>
#include <novacube/terrain.h>

#ifndef ANDROID
#define NC__BACKGROUND_DELAY 100
#endif

#define NC__MOVEMENT_SPEED 5.0f

typedef struct nc__app_t {
    nc_renderer_t* renderer;
    nc_terrain_t* terrain;
    nc_gui_context_t* gui;
    nc_camera_t camera;
    nc_block_type_t selected_type;
} nc__app_t;

static const bool* keyboard_state;

static void nc__app_modify_block(const nc__app_t* app, const nc_block_type_t block_type) {
    nc_terrain_modify_block(app->terrain, &app->camera, block_type);
}

static void nc__app_handle_gui_actions(const nc__app_t* app, const nc_gui_actions_t actions) {
    if (actions & NC_GUI_ACTION_PLACE_BLOCK) {
        nc__app_modify_block(app, app->selected_type);
    }
    if (actions & NC_GUI_ACTION_REMOVE_BLOCK) {
        nc__app_modify_block(app, NC_BLOCK_TYPE_AIR);
    }
}

static void nc__app_fini(nc__app_t* app) {
    if (!app) {
        return;
    }

    nc_terrain_fini(app->terrain, app->renderer);
    nc_gui_fini(app->gui, app->renderer);
    nc_renderer_fini(app->renderer);
    free(app);
}

SDL_AppResult SDL_AppInit(void** app_state, const int argc, char** argv) {
    (void)argc;
    (void)argv;

    SDL_SetAppMetadata(NC_PRODUCT_NAME, NC_VERSION, NC_PACKAGE_NAME);

    SDL_Log(NC_PRODUCT_NAME " " NC_VERSION "\n"
            "Build: " __DATE__ " " __TIME__ " " NC_BUILD_TYPE "\n"
            "Git: " NC_GIT_DESCRIBE "\n"
            "Commit: " NC_GIT_HASH);

    if (!nc_configuration_load()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load configuration: %s", SDL_GetError());
        SDL_ClearError();
    }

    nc__app_t* app = calloc(1, sizeof(*app));
    *app_state = app;

    app->camera = (nc_camera_t){
        .position = { { 127.5f, 127.5f, 124.0f } },
    };
    app->selected_type = NC_BLOCK_TYPE_STONE;

    app->renderer = nc_renderer_init(&(nc_renderer_create_info_t){
        .window_title = NC_PRODUCT_NAME " " NC_VERSION,
        .window_width = nc_config_get_resolution().x,
        .window_height = nc_config_get_resolution().y,
        .refresh_rate = nc_config_get_refresh_rate(),
        .video_mode = nc_config_get_video_mode(),
    });
    if (!app->renderer) {
        goto error;
    }

    app->gui = nc_gui_init(app->renderer);
    if (!app->gui) {
        goto error;
    }

    app->terrain = nc_terrain_init(app->renderer);
    if (!app->terrain) {
        goto error;
    }

    keyboard_state = SDL_GetKeyboardState(NULL);
    if (!nc_renderer_set_relative_mouse_mode(app->renderer, true)) {
        goto error;
    }

    return SDL_APP_CONTINUE;

error:
    nc_show_error_dialog();
    nc__app_fini(app);
    *app_state = NULL;
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* app_state) {
    nc__app_t* app = app_state;

    const Uint64 ticks = SDL_GetTicksNS();
    static Uint64 last_ticks = 0;
    static double time = 0.0;
    const double delta_time = last_ticks == 0 ? 1.0 / 60.0 : (double)(ticks - last_ticks) / 1000000000.0;
    time += delta_time;
    last_ticks = ticks;

    vkm_vec2 touch_camera_delta;
    nc_gui_get_camera_delta(app->gui, &touch_camera_delta);
    if (touch_camera_delta.x != 0.0f || touch_camera_delta.y != 0.0f) {
        const float sensitivity = (float)nc_config_get_touch_camera_stick_sensitivity();
        nc_camera_rotate(
                &app->camera,
                touch_camera_delta.x * sensitivity * (float)delta_time,
                touch_camera_delta.y * sensitivity * (float)delta_time);
    }

    vkm_vec2 touch_look_delta;
    if (nc_gui_consume_look_delta(app->gui, &touch_look_delta)) {
        const float sensitivity = (float)nc_config_get_touch_camera_drag_sensitivity();
        nc_camera_rotate(
                &app->camera,
                touch_look_delta.x * sensitivity,
                -touch_look_delta.y * sensitivity);
    }

    vkm_vec3 forward;
    vkm_vec3 right;
    vkm_vec3 up;
    nc_camera_get_basis(&app->camera, &forward, &right, &up);

    const nc_gui_controls_t gui_controls = nc_gui_get_controls(app->gui);
    vkm_vec3 input = { {
        (float)keyboard_state[SDL_SCANCODE_D] - (float)keyboard_state[SDL_SCANCODE_A],
        (float)keyboard_state[SDL_SCANCODE_SPACE] - (float)keyboard_state[SDL_SCANCODE_LCTRL],
        (float)keyboard_state[SDL_SCANCODE_W] - (float)keyboard_state[SDL_SCANCODE_S],
    } };
    input.x += (float)((gui_controls & NC_GUI_CONTROL_MOVE_RIGHT) != 0) - (float)((gui_controls & NC_GUI_CONTROL_MOVE_LEFT) != 0);
    input.y += (float)((gui_controls & NC_GUI_CONTROL_MOVE_UP) != 0) - (float)((gui_controls & NC_GUI_CONTROL_MOVE_DOWN) != 0);
    input.z += (float)((gui_controls & NC_GUI_CONTROL_MOVE_FORWARD) != 0) - (float)((gui_controls & NC_GUI_CONTROL_MOVE_BACKWARD) != 0);

    vkm_vec2 gui_movement_delta;
    nc_gui_get_movement_delta(app->gui, &gui_movement_delta);
    input.x += gui_movement_delta.x;
    input.z += gui_movement_delta.y;

    const float input_length = vkm_length(&input);
    if (input_length > 1.0f) {
        vkm_div(&input, input_length, &input);
    }

    vkm_vec3 velocity;
    vkm_mul(&right, input.x, &velocity);
    vkm_muladd(&CVKM_VEC3_UP, input.y, &velocity);
    vkm_muladd(&forward, input.z, &velocity);
    vkm_mul(&velocity, NC__MOVEMENT_SPEED, &velocity);
    vkm_muladd(&velocity, (float)delta_time, &app->camera.position);

    const vkm_usvec2 viewport = nc_renderer_get_viewport(app->renderer);
    vkm_mat4 view_projection;
    nc_camera_get_view_projection(
            &app->camera,
            vkm_deg2rad(80.0f),
            (float)viewport.x / (float)viewport.y,
            0.2f,
            500.0f,
            &view_projection);

    char debug_buffer[100];

    if (nc_config_get_show_fps()) {
        const int length = snprintf(debug_buffer, sizeof(debug_buffer), "%f FPS\n", 1.0 / delta_time);
        nc_gui_append_debug_text(app->gui, debug_buffer, length);
    }

    if (nc_config_get_show_frame_time()) {
        const int length = snprintf(debug_buffer, sizeof(debug_buffer), "%f ms\n", delta_time);
        nc_gui_append_debug_text(app->gui, debug_buffer, length);
    }

#ifndef ANDROID
    if (!nc_renderer_is_foreground(app->renderer)) {
        SDL_Delay(NC__BACKGROUND_DELAY);
    }
#endif

    if (!nc_renderer_begin_frame(app->renderer)) {
        goto error;
    }
    if (!nc_terrain_prepare_render(app->terrain, app->renderer)) {
        goto error;
    }
    if (!nc_gui_prepare_frame(app->gui, app->renderer, (float)delta_time)) {
        goto error;
    }

    nc_renderer_opaque_draw_t opaque_draw;
    nc_renderer_overlay_draw_t overlay_draw;
    nc_renderer_procedural_overlay_draw_t procedural_overlay_draw;
    nc_renderer_block_highlight_draw_t highlight_draw;
    nc_terrain_get_opaque_draw(app->terrain, &view_projection, &opaque_draw);
    nc_gui_get_overlay_draw(app->gui, &overlay_draw);
    nc_gui_get_procedural_overlay_draw(app->gui, &procedural_overlay_draw);
    nc_terrain_get_block_highlight_draw(app->terrain, &view_projection, (float)time, &app->camera, &highlight_draw);

    if (!nc_renderer_draw(app->renderer, &(nc_renderer_frame_t){
            .opaque_draws = &opaque_draw,
            .opaque_draw_count = 1,
            .overlay_draws = &overlay_draw,
            .overlay_draw_count = 1,
            .procedural_overlay_draw = &procedural_overlay_draw,
            .block_highlight_draw = &highlight_draw,
        })) {
        goto error;
    }

    if (!nc_renderer_end_frame(app->renderer)) {
        goto error;
    }

    return SDL_APP_CONTINUE;

error:
    nc_show_error_dialog();
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void* app_state, SDL_Event* event) {
    nc__app_t* app = app_state;

    if (!nc_renderer_handle_event(app->renderer, event)) {
        goto error;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        const vkm_usvec2 window_size = nc_renderer_get_window_size(app->renderer);
        nc_gui_set_window_size(app->gui, window_size.x, window_size.y);
    }

    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        const vkm_usvec2 viewport = nc_renderer_get_viewport(app->renderer);
        nc_gui_set_pixel_viewport(app->gui, viewport.x, viewport.y);
    }

    if (event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        nc_gui_set_window_display_scale(app->gui, nc_renderer_get_window_display_scale(app->renderer));
    }

    const bool gui_captured = nc_gui_handle_event(app->gui, event);
    nc__app_handle_gui_actions(app, nc_gui_consume_actions(app->gui));

    switch (event->type) {
        case SDL_EVENT_QUIT:
            SDL_Log("Received a quit event.");
            return SDL_APP_SUCCESS;
        case SDL_EVENT_TERMINATING:
            SDL_Log("Received a terminating event.");
            return SDL_APP_SUCCESS;
        case SDL_EVENT_MOUSE_MOTION:
            if (!gui_captured) {
                nc_camera_rotate(
                        &app->camera,
                        event->motion.xrel * vkm_deg2rad((float)nc_config_get_mouse_sensitivity()),
                        -event->motion.yrel * vkm_deg2rad((float)nc_config_get_mouse_sensitivity()));
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                if (!nc_renderer_set_relative_mouse_mode(app->renderer, false)) {
                    goto error;
                }
            } else if (event->key.scancode >= SDL_SCANCODE_1 && event->key.scancode <= SDL_SCANCODE_0) {
                app->selected_type = (event->key.scancode - SDL_SCANCODE_1) % NC_BLOCK_TYPE_COUNT + 1;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (gui_captured) {
                break;
            }

            if (event->button.button == SDL_BUTTON_RIGHT) {
                nc__app_modify_block(app, app->selected_type);
            } else if (event->button.button == SDL_BUTTON_LEFT) {
                nc__app_modify_block(app, NC_BLOCK_TYPE_AIR);
            }
            break;
        default:
            break;
    }

    return SDL_APP_CONTINUE;

error:
    nc_show_error_dialog();
    return SDL_APP_FAILURE;
}

void SDL_AppQuit(void* app_state, const SDL_AppResult result) {
    (void)result;

    SDL_Log("See you later!");

    nc__app_fini(app_state);
    SDL_Quit();
}
