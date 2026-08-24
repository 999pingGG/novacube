#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <clay.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#ifndef ANDROID
#include <novacube/argument_parser.h>
#endif
#include <novacube/asset_manager.h>
#include <novacube/build_info.h>
#include <novacube/camera.h>
#include <novacube/cvar.h>
#include <novacube/cvkm.h>
#include <novacube/entity_command.h>
#include <novacube/error_handling.h>
#include <novacube/gui.h>
#include <novacube/macros.h>
#include <novacube/player_input.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>
#include <novacube/terrain.h>

#ifndef ANDROID
#define NC__BACKGROUND_DELAY 100
#endif

#define NC__MOVEMENT_SPEED 5.0f

typedef struct nc__app_t {
    nc_asset_manager_t* asset_manager;
    nc_renderer_t* renderer;
    nc_terrain_t* terrain;
    nc_gui_context_t* gui;
    nc_player_input_t* player_input;
    nc_camera_t camera;
    nc_block_type_t selected_type;
    nc_renderer_chunk_draw_vec opaque_chunk_draws;
    nc_renderer_chunk_draw_vec transparent_chunk_draws;
} nc__app_t;

static void nc__app_modify_block(const nc__app_t* app, const nc_block_type_t block_type) {
    nc_terrain_entity_set_block(app->terrain, &app->camera, block_type);
}

static void nc__app_handle_player_actions(const nc__app_t* app, const nc_entity_actions_t actions) {
    if (actions & NC_ENTITY_ACTION_PLACE_BLOCK) {
        nc__app_modify_block(app, app->selected_type);
    }
    if (actions & NC_ENTITY_ACTION_REMOVE_BLOCK) {
        nc__app_modify_block(app, NC_BLOCK_TYPE_AIR);
    }
    if (actions & NC_ENTITY_ACTION_TOGGLE_CLAY_DEBUG_MODE) {
        Clay_SetDebugModeEnabled(!Clay_IsDebugModeEnabled());
    }
}

static void nc__app_apply_player_command(
    nc__app_t* app,
    const nc_entity_command_t* command,
    const float delta_time
) {
    nc__app_handle_player_actions(app, command->actions);
    nc_camera_rotate(&app->camera, command->look_delta.x, command->look_delta.y);

    vkm_vec3 forward;
    vkm_vec3 right;
    vkm_vec3 up;
    nc_camera_get_basis(&app->camera, &forward, &right, &up);
    forward.y = 0.0f;
    const float forward_length = vkm_length(&forward);
    if (forward_length > 0.0f) {
        vkm_div(&forward, forward_length, &forward);
    }

    vkm_vec3 velocity;
    vkm_mul(&right, command->movement.x, &velocity);
    vkm_muladd(&CVKM_VEC3_UP, command->movement.y, &velocity);
    vkm_muladd(&forward, command->movement.z, &velocity);
    vkm_mul(&velocity,
            NC__MOVEMENT_SPEED * (command->sprint ? 9.0f : 3.0f),
            &velocity);
    vkm_muladd(&velocity, delta_time, &app->camera.position);
    nc_terrain_update(app->terrain, app->renderer, &app->camera.position);
}

static void nc__app_fini(nc__app_t* app) {
    if (!app) {
        return;
    }

    nc_terrain_fini(app->terrain, app->renderer);
    nc_player_input_fini(app->player_input);
    nc_gui_fini(app->gui);
    nc_renderer_fini(app->renderer);
    nc_renderer_chunk_draw_vec_fini(&app->opaque_chunk_draws);
    nc_renderer_chunk_draw_vec_fini(&app->transparent_chunk_draws);
    nc_asset_manager_fini(app->asset_manager);
    free(app);
}

static void nc__app_print_build_info(void) {
    printf(NC_PRODUCT_NAME " " NC_VERSION "\n"
            "Build: " __DATE__ " " __TIME__ " " NC_BUILD_TYPE "\n"
            "Git: " NC_GIT_DESCRIBE "\n"
            "Commit: " NC_GIT_HASH "\n");
}

#ifndef ANDROID
static void nc__app_print_help(void) {
    printf(NC_PRODUCT_NAME " " NC_VERSION "\n\n"
            "Usage:\n"
            "  novacube\n"
            "  novacube --help\n"
            "  novacube --version\n"
            "  novacube --build-assets <source-directory> [changed-asset ...]\n"
            "           [-o <database>]\n"
            "           [--platform desktop|mobile]\n"
            "           [--texconv <executable>]\n"
            "           [--astcenc <executable>]\n"
            "           [--debug]\n"
            "           [--strip-png-metadata]\n\n"
            "Commands:\n"
            "  No arguments                         Run the game.\n"
            "  --help                               Print this help and exit.\n"
            "  --version                            Print build information and exit.\n"
            "  --build-assets <source-directory>    Bake assets from the source directory.\n\n"
            "Asset compiler arguments:\n"
            "  changed-asset                        Path relative to the source directory. Omit all\n"
            "                                       changed paths to rebuild every asset.\n"
            "  -o <database>                        Output database; defaults to assets.db.\n"
            "  --platform desktop|mobile            Output format; defaults to desktop.\n"
            "  --texconv <executable>                Texconv executable; defaults to texconv on PATH.\n"
            "  --astcenc <executable>                Astcenc executable; defaults to astcenc-avx2 on PATH.\n"
            "  --debug                              Compile shaders with debug information and no\n"
            "                                       optimization.\n"
            "  --strip-png-metadata                 Remove ancillary metadata from source PNGs\n"
            "                                       before baking them.\n");
}
#endif

//static void nc__app_animated_test_thingy(nc__app_t* app, const double time) {
//    const double radius = vkm_sin(time) * 5.0 + 10.0;
//
//    for (int z = -5; z <= 25; z++) {
//        for (int y = 5; y <= 35; y++) {
//            for (int x = -5; x <= 25; x++) {
//                const vkm_dvec3 center = (vkm_dvec3){ { x - 10.0, y - 20.0, z - 10.0 } };
//                const bool solid = vkm_sqr_magnitude(&center) <= radius * radius;
//                static const nc_block_type_t blocks[] = {
//                    NC_BLOCK_TYPE_STONE,
//                    NC_BLOCK_TYPE_DIRT,
//                    NC_BLOCK_TYPE_GRASS,
//                    NC_BLOCK_TYPE_TEST,
//                };
//
//                const nc_block_type_t block = solid ? blocks[rand() % NC_COUNTOF(blocks)] : NC_BLOCK_TYPE_AIR;
//                NC_ASSERT(block <= NC_BLOCK_TYPE_COUNT);
//                nc_terrain_set_block(app->terrain, &(vkm_ivec3){ { x, y, z } }, block);
//            }
//        }
//    }
//}

SDL_AppResult SDL_AppInit(void** app_state, const int argc, char** argv) {
    (void)argc;
    (void)argv;
    *app_state = NULL;
    nc__app_t* app = NULL;

    const bool sdl_success = SDL_SetAppMetadata(NC_PRODUCT_NAME, NC_VERSION, NC_PACKAGE_NAME);
    NC_CHECK_SDL_RESULT(sdl_success);

#ifndef ANDROID
    nc_arguments_t arguments;
    if (!nc_argument_parser_parse(argv, &arguments)) {
        // Can't show a messagebox, the program will run in headless mode in asset compiler mode.
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (arguments.action == NC_ARGUMENT_ACTION_PRINT_HELP) {
        nc__app_print_help();
        return SDL_APP_SUCCESS;
    }
    if (arguments.action == NC_ARGUMENT_ACTION_PRINT_VERSION) {
        nc__app_print_build_info();
        return SDL_APP_SUCCESS;
    }
#endif

    nc__app_print_build_info();

#ifndef ANDROID
    if (arguments.action == NC_ARGUMENT_ACTION_BUILD_ASSETS) {
        const bool result = nc_asset_manager_bake_assets(&arguments);
        if (!result) {
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        return SDL_APP_SUCCESS;
    }
#endif

    if (!nc_configuration_load()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load configuration: %s", SDL_GetError());
        SDL_ClearError();
    }

    app = calloc(1, sizeof(*app));
    *app_state = app;

    app->camera = (nc_camera_t){
        .position = { { 0.0f, 30.0f, 0.0f } },
    };
    app->selected_type = NC_BLOCK_TYPE_STONE;

    app->asset_manager = nc_asset_manager_init();
    if (!app->asset_manager) {
        goto error;
    }
    app->renderer = nc_renderer_init(&(nc_renderer_create_info_t){
        .window_title = NC_PRODUCT_NAME " " NC_VERSION,
        .window_width = nc_cvar_get_resolution().x,
        .window_height = nc_cvar_get_resolution().y,
        .refresh_rate = nc_cvar_get_refresh_rate(),
        .video_mode = nc_cvar_get_video_mode(),
        .prefer_low_power = nc_cvar_get_prefer_low_power_gpu(),
    }, app->asset_manager);
    if (!app->renderer) {
        goto error;
    }

    app->gui = nc_gui_init(app->renderer, app->asset_manager);
    if (!app->gui) {
        goto error;
    }

    app->player_input = nc_player_input_init(nc_gui_get_view(app->gui));

    app->terrain = nc_terrain_init(app->renderer, app->asset_manager);
    if (!app->terrain) {
        goto error;
    }

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

    //nc__app_animated_test_thingy(app, time);

    nc_player_input_update_view(app->player_input, nc_gui_get_view(app->gui));
    nc_player_input_update(app->player_input, (float)delta_time);

    nc_entity_command_t player_command;
    nc_player_input_get_entity_command(
            app->player_input,
            (float)delta_time,
            &player_command);
    nc__app_apply_player_command(app, &player_command, (float)delta_time);

    char debug_buffer[100];

    if (nc_cvar_get_show_fps()) {
        const int printed = snprintf(debug_buffer, sizeof(debug_buffer), "%f FPS\n", 1.0 / delta_time);
        nc_gui_append_debug_text(app->gui, debug_buffer, printed);
    }

    if (nc_cvar_get_show_frame_time()) {
        const int printed = snprintf(debug_buffer, sizeof(debug_buffer), "%f ms\n", delta_time);
        nc_gui_append_debug_text(app->gui, debug_buffer, printed);
    }

    if (nc_cvar_get_show_player_coords()) {
        const int printed = snprintf(
                debug_buffer,
                sizeof(debug_buffer),
                "Player: (%.3f, %.3f, %.3f)\n",
                app->camera.position.x,
                app->camera.position.y,
                app->camera.position.z);
        nc_gui_append_debug_text(app->gui, debug_buffer, printed);
    }

    if (nc_cvar_get_show_target_block_debug_details()) {
        nc_terrain_raycast_hit_t hit;
        if (nc_terrain_raycast(app->terrain, &app->camera, NC_TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE, &hit)) {
            const int printed = snprintf(
                    debug_buffer,
                    sizeof(debug_buffer),
                    "Block: (%d, %d, %d), distance: %f, top light blocking: %d\n",
                    hit.block_position.x,
                    hit.block_position.y,
                    hit.block_position.z,
                    hit.distance,
                    nc_terrain_get_top_light_blocking_block(
                            app->terrain,
                            (vkm_ivec2){ {
                                hit.block_position.x,
                                hit.block_position.z,
                            } }));
            nc_gui_append_debug_text(app->gui, debug_buffer, printed);
        }
    }

#ifndef ANDROID
    if (!nc_renderer_is_foreground(app->renderer)) {
        SDL_Delay(NC__BACKGROUND_DELAY);
    }
#endif

    if (!nc_renderer_begin_frame(app->renderer)) {
        goto error;
    }

    const vkm_usvec2 framebuffer_size = nc_renderer_get_framebuffer_size(app->renderer);

    vkm_mat4 view_projection;
    nc_camera_get_view_projection(
            &app->camera,
            vkm_deg2rad(80.0f),
            (float)framebuffer_size.x / (float)framebuffer_size.y,
            0.2f,
            1000.0f,
            &view_projection);

    if (!nc_terrain_prepare_render(app->terrain, app->renderer)) {
        goto error;
    }

    if (nc_cvar_get_show_terrain_timings()) {
        nc_terrain_timing_stats_t terrain_timings;
        nc_terrain_get_timing_stats(app->terrain, &terrain_timings);
        const int printed = snprintf(
                debug_buffer,
                sizeof(debug_buffer),
                "Terrain residency: %.3f ms, load: %.3f, unload: %.3f, lighting: %.3f, meshing: %.3f\n",
                terrain_timings.residency_ms,
                terrain_timings.loading_ms,
                terrain_timings.unloading_ms,
                terrain_timings.lighting_ms,
                terrain_timings.meshing_ms);
        nc_gui_append_debug_text(app->gui, debug_buffer, printed);
    }

    nc_terrain_frustum_culling_stats_t frustum_culling_stats;
    nc_terrain_get_chunk_draws(
            app->terrain,
            &view_projection,
            &app->opaque_chunk_draws,
            &app->transparent_chunk_draws,
            &frustum_culling_stats);
    if (nc_cvar_get_show_chunk_frustum_culling_stats()) {
        const int printed = snprintf(
                debug_buffer,
                sizeof(debug_buffer),
                "Chunks: %u loaded, %u empty, %u culled, %u opaque, %u transparent\n",
                frustum_culling_stats.loaded_chunk_count,
                frustum_culling_stats.empty_chunk_count,
                frustum_culling_stats.culled_chunk_count,
                frustum_culling_stats.opaque_drawn_chunk_count,
                frustum_culling_stats.transparent_drawn_chunk_count);
        nc_gui_append_debug_text(app->gui, debug_buffer, printed);
    }

    nc_player_input_overlay_t player_input_overlay;
    nc_player_input_get_overlay(app->player_input, &player_input_overlay);
    nc_gui_prepare_frame(app->gui, &player_input_overlay, (float)delta_time);

    nc_renderer_overlay_draw_t overlay_draw;
    nc_gui_get_overlay_draw(app->gui, &overlay_draw);
    nc_renderer_procedural_overlay_draw_t procedural_overlay_draw;
    nc_gui_get_procedural_overlay_draw(&player_input_overlay, &procedural_overlay_draw);
    nc_renderer_block_highlight_draw_t highlight_draw;
    nc_terrain_get_block_highlight_draw(app->terrain, (float)time, &app->camera, &highlight_draw);

    static const nc_renderer_sky_draw_t day_sky_draw = {
        .gradient_colors = {
            // Linear equivalents of sRGB #29335c, #598cd9, #1f52b3 and #06143d.
            { { 0.0222271f, 0.0331047f, 0.1071563f, 1.0f } },
            { { 0.0998870f, 0.2622302f, 0.6939078f, 1.0f } },
            { { 0.0137825f, 0.0846083f, 0.4508418f, 1.0f } },
            { { 0.0018575f, 0.0069412f, 0.0465830f, 1.0f } },
        },
        .gradient_stops = { { -0.25f, 0.0f, 0.40f, 1.0f } },
    };

    static const nc_renderer_sky_draw_t night_sky_draw = {
        .gradient_colors = {
            // Linear equivalents of sRGB #020308, #33264f, #101b3d and #030817.
            { { 0.0006191f, 0.0009287f, 0.0023993f, 1.0f } },
            { { 0.0331047f, 0.0193778f, 0.0782883f, 1.0f } },
            { { 0.0052084f, 0.0109793f, 0.0465830f, 1.0f } },
            { { 0.0009287f, 0.0023993f, 0.0085403f, 1.0f } },
        },
        .gradient_stops = { { -0.18f, 0.03f, 0.42f, 1.0f } },
    };

#ifdef NC_DO_DAY_NIGHT_CYLE
    const float time_of_day = vkm_clamp(vkm_sin((float)time * 0.2f) * 0.96f + 0.6f, 0.0f, 1.0f);
#else
    const float time_of_day = 1.0f;
#endif

    nc_renderer_sky_draw_t sky_draw;
    vkm_lerp(
            &night_sky_draw.gradient_colors[0],
            &day_sky_draw.gradient_colors[0],
            time_of_day,
            &sky_draw.gradient_colors[0]);
    vkm_lerp(
            &night_sky_draw.gradient_colors[1],
            &day_sky_draw.gradient_colors[1],
            time_of_day,
            &sky_draw.gradient_colors[1]);
    vkm_lerp(
            &night_sky_draw.gradient_colors[2],
            &day_sky_draw.gradient_colors[2],
            time_of_day,
            &sky_draw.gradient_colors[2]);
    vkm_lerp(
            &night_sky_draw.gradient_colors[3],
            &day_sky_draw.gradient_colors[3],
            time_of_day,
            &sky_draw.gradient_colors[3]);
    vkm_lerp(&night_sky_draw.gradient_stops, &day_sky_draw.gradient_stops, time_of_day, &sky_draw.gradient_stops);

    const bool success = nc_renderer_draw(app->renderer, &(nc_renderer_frame_t){
        .view_projection = &view_projection,
        .camera_position = app->camera.position,
        .sunlight_intensity = time_of_day * 0.98f + 0.02f,
        .sky_draw = &sky_draw,
        .opaque_chunk_draws = &app->opaque_chunk_draws,
        .transparent_chunk_draws = &app->transparent_chunk_draws,
        .overlay_draws = &overlay_draw,
        .overlay_draw_count = 1,
        .procedural_overlay_draw = &procedural_overlay_draw,
        .block_highlight_draw = &highlight_draw,
    });
    nc_renderer_chunk_draw_vec_clear(&app->opaque_chunk_draws);
    nc_renderer_chunk_draw_vec_clear(&app->transparent_chunk_draws);
    if (!success) {
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

    const bool gui_captured_event = nc_gui_handle_event(app->gui, event);
    if (!gui_captured_event && !nc_player_input_handle_event(app->player_input, app->renderer, event)) {
        goto error;
    }

    switch (event->type) {
        case SDL_EVENT_QUIT:
            SDL_Log("Received a quit event.");
            return SDL_APP_SUCCESS;
        case SDL_EVENT_TERMINATING:
            SDL_Log("Received a terminating event.");
            return SDL_APP_SUCCESS;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                if (!nc_renderer_set_relative_mouse_mode(app->renderer, false)) {
                    goto error;
                }
            } else if (event->key.scancode >= SDL_SCANCODE_1 && event->key.scancode <= SDL_SCANCODE_0) {
                app->selected_type = (event->key.scancode - SDL_SCANCODE_1) % NC_BLOCK_TYPE_COUNT + 1;
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

    if (app_state) {
        SDL_Log("See you later!");
        nc__app_fini(app_state);
    }
    SDL_Quit();
}
