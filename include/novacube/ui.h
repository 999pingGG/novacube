#ifndef _NOVACUBE_UI_H_
#define _NOVACUBE_UI_H_
#include <clay.h>

bool nc_ui_init(SDL_GPUDevice* device, const vkm_usvec2 viewport_size);
void nc_render_clay_command_array(
        SDL_GPUDevice* device,
        float delta_time,
        const vkm_usvec2 viewport_size,
        const Clay_RenderCommandArray* commands);
void nc_upload_pending_ui_textures(SDL_GPUDevice* device, SDL_GPUCopyPass* copy_pass);
bool nc_draw_2d(SDL_GPUDevice* device);
void nc_ui_fini(SDL_GPUDevice* device);
#endif
