#include <stdbool.h>
#include <stdio.h>

#include <cvkm.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#define NC_CHECK_SDL_RESULT(result) do { \
    if (!result) { \
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", SDL_GetError(), NULL); \
        goto error; \
    } \
} while (false)

static SDL_GPUDevice* device;
static SDL_Window* window;
static SDL_GPUTexture* depth_texture;
static vkm_usvec2 viewport_size;

#ifndef ANDROID
#define NC_BACKGROUND_DELAY 100
#endif
static bool nc__foreground = true;

SDL_AppResult SDL_AppInit(void** app_state, const int argc, char** argv) {
    (void)app_state;
    (void)argc;
    (void)argv;

    SDL_PropertiesID props = 0;

    bool sdl_result = SDL_InitSubSystem(SDL_INIT_VIDEO);
    NC_CHECK_SDL_RESULT(sdl_result);

    props = SDL_CreateProperties();
    NC_CHECK_SDL_RESULT(props);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
#ifndef NDEBUG
            true);
#else
            false);
#endif
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(
            props,
            SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,
            true);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, true);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
    NC_CHECK_SDL_RESULT(sdl_result);
    sdl_result = SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
    NC_CHECK_SDL_RESULT(sdl_result);
    device = SDL_CreateGPUDeviceWithProperties(props);
    NC_CHECK_SDL_RESULT(device);
    SDL_DestroyProperties(props);
    props = 0;

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#ifdef ANDROID
    // Android always has a fullscreen
    flags |= SDL_WINDOW_FULLSCREEN;
#endif

    window = SDL_CreateWindow("Novacube", 640, 480, flags);
    NC_CHECK_SDL_RESULT(window);
    // get the actual window dimensions
    int width, height;
    sdl_result = SDL_GetWindowSize(window, &width, &height);
    NC_CHECK_SDL_RESULT(sdl_result);
    viewport_size.x = (uint16_t)width;
    viewport_size.y = (uint16_t)height;

    sdl_result = SDL_ClaimWindowForGPUDevice(device, window);
    NC_CHECK_SDL_RESULT(sdl_result);

    depth_texture = SDL_CreateGPUTexture(
            device,
            &(SDL_GPUTextureCreateInfo) {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                .width = width,
                .height = height,
                .layer_count_or_depth = 1,
                .num_levels = 1,
                .sample_count = SDL_GPU_SAMPLECOUNT_1,
            });

    return SDL_APP_CONTINUE;

    error:
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    window = NULL;
    SDL_DestroyGPUDevice(device);
    device = NULL;
    SDL_DestroyProperties(props);
    props = 0;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* app_state) {
    (void)app_state;

    SDL_GPUCommandBuffer* command_buffer = NULL;

#ifndef ANDROID
    if (!nc__foreground) {
        SDL_Delay(NC_BACKGROUND_DELAY);
    }
#endif

    command_buffer = SDL_AcquireGPUCommandBuffer(device);
    NC_CHECK_SDL_RESULT(command_buffer);

    SDL_GPUTexture* swapchain_texture;
    bool sdl_result = SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL);
    NC_CHECK_SDL_RESULT(sdl_result);
    if (swapchain_texture) {
        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
                command_buffer,
                &(SDL_GPUColorTargetInfo){
                    .texture = swapchain_texture,
                    .clear_color = { 0.53f, 0.81f, 0.92f, 1.0f },
                    .load_op = SDL_GPU_LOADOP_CLEAR,
                    .store_op = SDL_GPU_STOREOP_STORE,
                },
                1,
                NULL);
        SDL_EndGPURenderPass(render_pass);
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
    return SDL_APP_CONTINUE;

    error:
    SDL_SubmitGPUCommandBuffer(command_buffer);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", SDL_GetError(), NULL);
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void* app_state, SDL_Event* event) {
    (void)app_state;

    switch (event->type) {
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_MINIMIZED:
            nc__foreground = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            // recreate depth texture with new dimensions
            SDL_ReleaseGPUTexture(device, depth_texture);
            viewport_size = (vkm_usvec2) {
                    .x = (uint16_t)event->window.data1,
                    .y = (uint16_t)event->window.data2,
            };
            depth_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo) {
                    .type = SDL_GPU_TEXTURETYPE_2D,
                    .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                    .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                    .width = viewport_size.x,
                    .height = viewport_size.y,
                    .layer_count_or_depth = 1,
                    .num_levels = 1,
                    .sample_count = SDL_GPU_SAMPLECOUNT_1,
            });
            if (!depth_texture) {

            }
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            nc__foreground = true;
            break;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_TERMINATING:
            nc__foreground = false;
            return SDL_APP_SUCCESS;
        default:
            break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* app_state, const SDL_AppResult result) {
    (void)app_state;
    (void)result;

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    window = NULL;
    SDL_DestroyGPUDevice(device);
    device = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
}
