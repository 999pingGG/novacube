#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <novacube/macros.h>
#define CLAY_IMPLEMENTATION
NC_IGNORE_ALL_WARNINGS_BEGIN
#include <clay.h>
NC_IGNORE_ALL_WARNINGS_END
#include <SDL3/SDL.h>

#include <novacube/asset_manager.h>
#include <novacube/cvar.h>
#include <novacube/cvkm.h>
#include <novacube/gui.h>
#include <novacube/player_input.h>
#include <novacube/renderer.h>
#include <novacube/standard_functions.h>
#include <novacube/string_handling.h>

// Spleen 5x8, packed one bit per pixel into a 64x64 atlas. Character 127 is a diamond fallback.
static const uint8_t nc__spleen_font_bits[512] = {
    0x80, 0x28, 0x40, 0x10, 0x21, 0x48, 0x00, 0x00,   0x80, 0x28, 0xe5, 0x92, 0x22, 0x84, 0x00, 0x00,
    0x80, 0xa8, 0x5f, 0x8a, 0x22, 0x02, 0x25, 0x02,   0x80, 0x00, 0x65, 0x88, 0x01, 0x02, 0x19, 0x02,
    0x80, 0x00, 0xc5, 0x44, 0x05, 0x02, 0xbd, 0x0f,   0x00, 0x80, 0xcf, 0x54, 0x02, 0x02, 0x19, 0x02,
    0x80, 0x00, 0x75, 0x92, 0x05, 0x84, 0x24, 0x02,   0x00, 0x00, 0x40, 0x02, 0x00, 0x48, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x64, 0x88, 0x31, 0xe1, 0x99, 0x07,
    0x00, 0x00, 0x92, 0x4c, 0x4a, 0x25, 0x84, 0x04,   0x00, 0x00, 0xd2, 0x08, 0x22, 0xe5, 0x1c, 0x04,
    0xe0, 0x01, 0xb1, 0x88, 0x41, 0x0f, 0x25, 0x02,   0x04, 0x00, 0x91, 0x48, 0x48, 0x04, 0x25, 0x01,
    0x04, 0x90, 0x60, 0xdc, 0x33, 0xe4, 0x18, 0x01,   0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,   0xc6, 0x00, 0x80, 0x80, 0x48, 0xc6, 0x1c, 0x07,
    0x29, 0x01, 0x40, 0x00, 0x41, 0x29, 0xa5, 0x00,   0x26, 0x11, 0x22, 0x1e, 0x22, 0x2d, 0x9d, 0x00,
    0xc9, 0x01, 0x20, 0x00, 0x12, 0xed, 0xa5, 0x00,   0x09, 0x01, 0x42, 0x1e, 0x01, 0x21, 0xa5, 0x00,
    0xc6, 0x10, 0x82, 0x80, 0x10, 0x2e, 0x1d, 0x07,   0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0xc7, 0x39, 0x97, 0x9c, 0x4b, 0x21, 0x25, 0x03,
    0x29, 0x84, 0x90, 0x08, 0x49, 0xe1, 0xad, 0x04,   0xe9, 0x84, 0xf6, 0x08, 0x39, 0xe1, 0xad, 0x04,
    0x29, 0x9c, 0x94, 0x08, 0x49, 0x21, 0xb5, 0x04,   0x29, 0x84, 0x94, 0x08, 0x49, 0x21, 0xb5, 0x04,
    0xc7, 0x05, 0x97, 0xdc, 0x48, 0x2e, 0x25, 0x03,   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,   0xc7, 0x1c, 0xf7, 0x53, 0x4a, 0x29, 0x3d, 0x01,
    0x29, 0xa5, 0x40, 0x52, 0x4a, 0x29, 0x21, 0x01,   0x29, 0x25, 0x43, 0x52, 0x4a, 0x26, 0x11, 0x01,
    0x27, 0x1d, 0x44, 0x52, 0x7a, 0xc6, 0x09, 0x01,   0x21, 0x25, 0x44, 0x92, 0x79, 0x09, 0x05, 0x01,
    0xc1, 0xa4, 0x43, 0x9c, 0x49, 0xe9, 0x3c, 0x01,   0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
    0xc1, 0x01, 0x20, 0x40, 0x00, 0x08, 0x30, 0x00,   0x01, 0x11, 0x40, 0x40, 0x00, 0x08, 0x08, 0x00,
    0x02, 0x29, 0x00, 0xcc, 0x71, 0xce, 0x09, 0x07,   0x02, 0x45, 0x00, 0x50, 0x0a, 0x29, 0x9d, 0x04,
    0x04, 0x01, 0x00, 0x5c, 0x0a, 0xe9, 0x89, 0x04,   0x04, 0x01, 0x00, 0x52, 0x0a, 0x29, 0x08, 0x03,
    0x08, 0x01, 0x00, 0xdc, 0x71, 0xce, 0x09, 0x04,   0xc8, 0x81, 0x07, 0x00, 0x00, 0x00, 0x80, 0x03,
    0x01, 0x80, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,   0x81, 0x90, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x80, 0x24, 0xd2, 0x31, 0xc7, 0x39, 0x07,   0xc9, 0x90, 0x22, 0x5e, 0x4a, 0x29, 0xa5, 0x00,
    0x89, 0x90, 0x21, 0x5e, 0x4a, 0x29, 0x05, 0x03,   0x89, 0x90, 0x22, 0x52, 0x4a, 0xc7, 0x05, 0x04,
    0x89, 0x91, 0xc4, 0x52, 0x32, 0x01, 0x85, 0x03,   0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x60, 0x64, 0x00, 0x02,   0x02, 0x00, 0x00, 0x00, 0x10, 0x84, 0x00, 0x07,
    0x27, 0xa5, 0x94, 0xd2, 0x13, 0x84, 0x00, 0x07,   0x22, 0xa5, 0x64, 0x12, 0x1a, 0x84, 0xc9, 0x0f,
    0x22, 0xa5, 0x67, 0x12, 0x19, 0x84, 0xb5, 0x0f,   0x22, 0x99, 0x97, 0x9c, 0x10, 0x84, 0x00, 0x07,
    0xcc, 0x99, 0x94, 0xd0, 0x13, 0x84, 0x00, 0x07,   0x00, 0x00, 0x00, 0x0e, 0x60, 0x64, 0x00, 0x02,
};

#define NC__GUI_FONT_WIDTH 5
#define NC__GUI_FONT_HEIGHT 8
// Clay multiplies scroll deltas by 10. SDL reports one conventional wheel detent as 1, so
// multiplying by three moves one 30-pixel inspector row while preserving trackpad fractions.
#define NC__GUI_MOUSE_WHEEL_SCALE 3.0f
// Render commands use these stacks for nested clips and color overlays. Clay emits balanced
// start/end commands, so the limit only needs to cover nesting authored by the GUI.
#define NC__GUI_STACK_CAPACITY 64

#define TDS_TYPE nc__gui_rectangle_vec
#define TDS_VALUE_T nc_renderer_overlay_rectangle_t
#include <tds/vector.h>

#define TDS_TYPE nc__gui_command_vec
#define TDS_VALUE_T nc_renderer_overlay_draw_command_t
#include <tds/vector.h>

typedef struct nc_gui_context_t {
    nc_renderer_t* renderer;
    void* clay_memory;
    nc_gui_view_t view;
    SDL_FingerID captured_finger;
    bool captured_finger_active;
    uint8_t captured_mouse_button;
    Clay_Vector2 pointer_position;
    Clay_Vector2 scroll_delta;
    bool pointer_down;
    nc_renderer_texture_t* font_texture;
    nc_renderer_texture_t* control_textures[NC_PLAYER_INPUT_CONTROL_COUNT];
    nc_string_builder_t debug_string_builder;

    // Retained vectors own the draw list until nc_renderer_draw() consumes it later in the frame.
    nc__gui_rectangle_vec rectangles;
    nc__gui_command_vec commands;

    // Avoid logging one warning per frame for an unsupported custom command.
    uint64_t last_custom_warning;
} nc_gui_context_t;

static const char* nc__gui_control_textures[NC_PLAYER_INPUT_CONTROL_COUNT] = {
    "left",
    "right",
    "up",
    "down",
    "up",
    "down",
    "place",
    "remove",
};

// GUI colors are treated as linear values in Clay's conventional 0..255 range. The sRGB
// swapchain performs the final display encoding.
static vkm_vec4 nc__gui_color_from_clay_color(const Clay_Color color) {
    return (vkm_vec4){ { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f } };
}

// Nested clips may only draw inside both their own bounds and every parent clip.
static SDL_FRect nc__gui_intersect_rects(const SDL_FRect a, const SDL_FRect b) {
    const float left = vkm_max(a.x, b.x);
    const float top = vkm_max(a.y, b.y);
    const float right = vkm_min(a.x + a.w, b.x + b.w);
    const float bottom = vkm_min(a.y + a.h, b.y + b.h);
    return (SDL_FRect){ left, top, vkm_max(right - left, 0.0f), vkm_max(bottom - top, 0.0f) };
}

// Converts SDL's window-coordinate safe area into the framebuffer coordinates used by Clay.
static void nc__gui_update_view(nc_gui_context_t* context) {
    const vkm_usvec2 framebuffer_size = nc_renderer_get_framebuffer_size(context->renderer);
    const vkm_usvec2 window_size = nc_renderer_get_window_size(context->renderer);
    context->view.framebuffer_size = framebuffer_size;

    SDL_Rect window_safe_area;
    nc_renderer_get_window_safe_area(context->renderer, &window_safe_area);
    // A minimized desktop window and an Android surface transition can temporarily report zero.
    if (window_size.x == 0 || window_size.y == 0) {
        context->view.gui_safe_area = (SDL_FRect){ 0 };
        return;
    }

    const float window_to_framebuffer_x = (float)framebuffer_size.x / (float)window_size.x;
    const float window_to_framebuffer_y = (float)framebuffer_size.y / (float)window_size.y;
    const SDL_FRect safe_area = {
        (float)window_safe_area.x * window_to_framebuffer_x,
        (float)window_safe_area.y * window_to_framebuffer_y,
        (float)window_safe_area.w * window_to_framebuffer_x,
        (float)window_safe_area.h * window_to_framebuffer_y,
    };
    const SDL_FRect canvas = { 0.0f, 0.0f, framebuffer_size.x, framebuffer_size.y };
    context->view.gui_safe_area = nc__gui_intersect_rects(safe_area, canvas);
}

static Clay_Vector2 nc__gui_window_to_clay_position(const nc_gui_context_t* context, const float x, const float y) {
    const vkm_usvec2 window_size = nc_renderer_get_window_size(context->renderer);
    if (window_size.x == 0 || window_size.y == 0) {
        return (Clay_Vector2){ 0 };
    }

    return (Clay_Vector2){
        x * (float)context->view.framebuffer_size.x / (float)window_size.x -
                context->view.gui_safe_area.x,
        y * (float)context->view.framebuffer_size.y / (float)window_size.y -
                context->view.gui_safe_area.y,
    };
}

static Clay_Vector2 nc__gui_touch_to_clay_position(const nc_gui_context_t* context, const float x, const float y) {
    return (Clay_Vector2){
        x * (float)context->view.framebuffer_size.x - context->view.gui_safe_area.x,
        y * (float)context->view.framebuffer_size.y - context->view.gui_safe_area.y,
    };
}

static bool nc__gui_pointer_over_element(const nc_gui_context_t* context, const Clay_Vector2 position) {
    Clay_SetPointerState(position, context->pointer_down);
    // Pass-through floating roots allow Clay to continue hit-testing the implicit root container.
    // A capturing overlay, such as the debug inspector, stops the search before that root is reached.
    return Clay_GetPointerOverIds().length > 0 && !Clay_PointerOver(CLAY_ID("Clay__RootContainer"));
}

static void nc__gui_clay_error_handler(const Clay_ErrorData error) {
    SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Clay: %.*s",
            (int)error.errorText.length,
            error.errorText.chars);
}

static Clay_Dimensions nc__gui_measure_text(
    const Clay_StringSlice text,
    Clay_TextElementConfig* config,
    void* user_data
) {
    (void)user_data;

    const float glyph_width = (float)config->fontSize * NC__GUI_FONT_WIDTH / NC__GUI_FONT_HEIGHT;
    const float line_height = (float)(config->lineHeight ? config->lineHeight : config->fontSize);
    float width = 0.0f;
    float max_width = 0.0f;
    float height = line_height;
    for (int32_t i = 0; i < text.length; i++) {
        const char character = text.chars[i];
        if (character == '\r' && i + 1 < text.length && text.chars[i + 1] == '\n') {
            continue;
        }
        if (character == '\r' || character == '\n') {
            max_width = vkm_max(max_width, width);
            width = 0.0f;
            height += line_height;
        } else {
            width += (glyph_width + (float)config->letterSpacing) * (character == '\t' ? 4.0f : 1.0f);
        }
    }

    return (Clay_Dimensions){ vkm_max(max_width, width), text.length ? height : 0.0f };
}

// Adds a solid, border, or glyph instance and extends the preceding compatible batch.
static void nc__gui_append_rectangle(
    nc_gui_context_t* context,
    const nc_renderer_overlay_rectangle_t* rectangle,
    const bool clip_enabled,
    const SDL_FRect clip
) {
    if (rectangle->rectangle.w <= 0 || rectangle->rectangle.h <= 0 || (clip_enabled && (clip.w <= 0 || clip.h <= 0))) {
        return;
    }

    const uint32_t index = nc__gui_rectangle_vec_count(&context->rectangles);
    nc__gui_rectangle_vec_append(&context->rectangles, *rectangle);

    const uint32_t command_count = nc__gui_command_vec_count(&context->commands);
    if (command_count) {
        nc_renderer_overlay_draw_command_t* previous = &context->commands.array[command_count - 1];
        if (    previous->type == NC_RENDERER_OVERLAY_COMMAND_RECTANGLES
                && previous->clip_enabled == clip_enabled
                && (!clip_enabled || memcmp(&previous->clip_rect, &clip, sizeof(clip)) == 0)
                && previous->rectangles.first_rectangle + previous->rectangles.rectangle_count == index) {
            previous->rectangles.rectangle_count++;
            return;
        }
    }

    nc__gui_command_vec_append(&context->commands, (nc_renderer_overlay_draw_command_t){
        .type = NC_RENDERER_OVERLAY_COMMAND_RECTANGLES,
        .clip_enabled = clip_enabled,
        .clip_rect = clip,
        .rectangles = { .first_rectangle = index, .rectangle_count = 1 },
    });
}

// Collapses two nested Clay color overlays into one equivalent shader operation.
static vkm_vec4 nc__gui_compose_overlay(const vkm_vec4 bottom, const vkm_vec4 top) {
    const float alpha = bottom.a + top.a * (1.0f - bottom.a);
    if (alpha <= 0.0f) {
        return (vkm_vec4){ 0 };
    }
    return (vkm_vec4){ {
        (bottom.r * bottom.a * (1.0f - top.a) + top.r * top.a) / alpha,
        (bottom.g * bottom.a * (1.0f - top.a) + top.g * top.a) / alpha,
        (bottom.b * bottom.a * (1.0f - top.a) + top.b * top.a) / alpha,
        alpha,
    } };
}

// Expands Spleen text into glyph rectangle instances. Measurement and emission intentionally use
// the same glyph advance and line-height rules.
static void nc__gui_append_text(
    nc_gui_context_t* context,
    const Clay_RenderCommand* command,
    const bool clip_enabled,
    const SDL_FRect clip,
    const vkm_vec4 overlay
) {
    const Clay_TextRenderData* text = &command->renderData.text;
    if (!text->stringContents.length || command->boundingBox.width <= 0 || command->boundingBox.height <= 0) {
        return;
    }
    const float glyph_width = vkm_max(
            (float)text->fontSize * NC__GUI_FONT_WIDTH / NC__GUI_FONT_HEIGHT,
            1.0f);
    const float glyph_height = vkm_max((float)text->fontSize, 1.0f);
    const float line_height = text->lineHeight ? text->lineHeight : glyph_height;
    const float origin_x = command->boundingBox.x + context->view.gui_safe_area.x;
    float x = origin_x;
    float y = command->boundingBox.y + context->view.gui_safe_area.y;
    for (int32_t i = 0; i < text->stringContents.length; i++) {
        uint8_t character = (uint8_t)text->stringContents.chars[i];
        if (character == '\r' && i + 1 < text->stringContents.length &&
                text->stringContents.chars[i + 1] == '\n') {
            continue;
        }
        if (character == '\r' || character == '\n') {
            x = origin_x;
            y += line_height;
            continue;
        }
        if (character == '\t') {
            x += (glyph_width + text->letterSpacing) * 4;
            continue;
        }
        if (character < 32 || character > 127) {
            character = 127;
        }
        nc__gui_append_rectangle(
                context,
                &(nc_renderer_overlay_rectangle_t){
                    .rectangle = { x, y, glyph_width, glyph_height },
                    .color = nc__gui_color_from_clay_color(text->textColor),
                    .overlay_color = overlay,
                    .character = character,
                },
                clip_enabled,
                clip);
        x += glyph_width + text->letterSpacing;
    }
}

static vkm_vec4 nc__gui_vec4_from_corner_radii(const Clay_CornerRadius* radii) {
    return (vkm_vec4){ { radii->topLeft, radii->topRight, radii->bottomLeft, radii->bottomRight } };
}

static SDL_FRect nc__gui_rect_from_clay(const nc_gui_context_t* context, const Clay_BoundingBox* box) {
    return (SDL_FRect){
        box->x + context->view.gui_safe_area.x,
        box->y + context->view.gui_safe_area.y,
        box->width,
        box->height,
    };
}

// Converts Clay's heterogeneous command array into the renderer's ordered rectangle/image stream.
static void nc__gui_build_draw_list(nc_gui_context_t* context, Clay_RenderCommandArray clay_commands) {
    SDL_FRect clips[NC__GUI_STACK_CAPACITY];
    vkm_vec4 overlays[NC__GUI_STACK_CAPACITY] = { 0 };
    uint32_t clip_count = 0;
    uint32_t overlay_count = 1;
    nc__gui_rectangle_vec_clear(&context->rectangles);
    nc__gui_command_vec_clear(&context->commands);

    for (int32_t i = 0; i < clay_commands.length; i++) {
        const Clay_RenderCommand* command = Clay_RenderCommandArray_Get(&clay_commands, i);
        const bool clipped = clip_count != 0;
        const SDL_FRect clip = clipped ? clips[clip_count - 1] : (SDL_FRect){ 0 };
        const vkm_vec4 overlay = overlays[overlay_count - 1];
        switch (command->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_NONE:
                break;
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
                nc__gui_append_rectangle(
                        context,
                        &(nc_renderer_overlay_rectangle_t){
                            .rectangle = nc__gui_rect_from_clay(context, &command->boundingBox),
                            .color = nc__gui_color_from_clay_color(command->renderData.rectangle.backgroundColor),
                            .corner_radii = nc__gui_vec4_from_corner_radii(&command->renderData.rectangle.cornerRadius),
                            .overlay_color = overlay,
                        },
                        clipped,
                        clip);
                break;
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                const Clay_BorderWidth* width = &command->renderData.border.width;
                // Clay emits betweenChildren dividers as separate rectangle commands. A BORDER
                // command with no outer sides must therefore not become a filled bounding box.
                if (!(width->left || width->right || width->top || width->bottom)) {
                    break;
                }
                nc__gui_append_rectangle(
                        context,
                        &(nc_renderer_overlay_rectangle_t){
                            .rectangle = nc__gui_rect_from_clay(context, &command->boundingBox),
                            .color = nc__gui_color_from_clay_color(command->renderData.border.color),
                            .corner_radii = nc__gui_vec4_from_corner_radii(&command->renderData.border.cornerRadius),
                            .border_widths = { {
                                width->left,
                                width->right,
                                width->top,
                                width->bottom,
                            } },
                            .overlay_color = overlay,
                        },
                        clipped,
                        clip);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
                nc__gui_append_text(context, command, clipped, clip, overlay);
                break;
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                if (!command->renderData.image.imageData || command->boundingBox.width <= 0 ||
                        command->boundingBox.height <= 0 || (clipped && (clip.w <= 0 || clip.h <= 0))) {
                    break;
                }
                Clay_Color tint = command->renderData.image.backgroundColor;
                // Clay reserves exactly transparent black as the default "untinted" image color.
                if (tint.r == 0.0f && tint.g == 0.0f && tint.b == 0.0f && tint.a == 0.0f) {
                    tint = (Clay_Color){ 255, 255, 255, 255 };
                }
                nc__gui_command_vec_append(&context->commands, (nc_renderer_overlay_draw_command_t){
                    .type = NC_RENDERER_OVERLAY_COMMAND_IMAGE,
                    .clip_enabled = clipped,
                    .clip_rect = clip,
                    .image = {
                        .texture = command->renderData.image.imageData,
                        .rectangle = nc__gui_rect_from_clay(context, &command->boundingBox),
                        .color = nc__gui_color_from_clay_color(tint),
                        .corner_radii = nc__gui_vec4_from_corner_radii(&command->renderData.image.cornerRadius),
                        .overlay_color = overlay,
                    },
                });
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                NC_ASSERT(clip_count < NC__GUI_STACK_CAPACITY);
                SDL_FRect next = context->view.gui_safe_area;
                const SDL_FRect bounds = nc__gui_rect_from_clay(context, &command->boundingBox);
                // Floating roots inherit a clip through a Clay-generated command whose axis flags
                // are both zero. Clay still documents that command as clipping to its whole bounds.
                const bool inherited_clip =
                        !command->renderData.clip.horizontal && !command->renderData.clip.vertical;
                if (command->renderData.clip.horizontal || inherited_clip) {
                    next.x = bounds.x;
                    next.w = bounds.w;
                }
                if (command->renderData.clip.vertical || inherited_clip) {
                    next.y = bounds.y;
                    next.h = bounds.h;
                }
                if (clip_count) {
                    next = nc__gui_intersect_rects(clips[clip_count - 1], next);
                }
                clips[clip_count++] = next;
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                if (clip_count) {
                    clip_count--;
                }
                break;
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
                NC_ASSERT(overlay_count < NC__GUI_STACK_CAPACITY);
                overlays[overlay_count] = nc__gui_compose_overlay(
                        overlays[overlay_count - 1],
                        nc__gui_color_from_clay_color(command->renderData.overlayColor.color));
                overlay_count++;
                break;
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
                if (overlay_count > 1) {
                    overlay_count--;
                }
                break;
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
                if (!context->last_custom_warning || SDL_GetTicks() - context->last_custom_warning >= 5000) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Skipping unsupported Clay custom command");
                    context->last_custom_warning = SDL_GetTicks();
                }
                break;
        }
    }
}

// Declares the touchscreen buttons and debug text to Clay. Analog sticks and the crosshair
// are procedural renderer passes and are not part of this layout.
static void nc__gui_build_hud(nc_gui_context_t* context, const nc_player_input_overlay_t* player_input) {
    Clay_BeginLayout();

    CLAY(CLAY_ID("HUD"), {
        .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) } },
        .floating = {
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
            .attachTo = CLAY_ATTACH_TO_ROOT,
        },
    }) {
        if (player_input->touch_controls_enabled) {
            for (uint32_t i = 0; i < NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
                if (!player_input->controls_visible[i]) {
                    continue;
                }
                const SDL_FRect rect = player_input->control_rects[i];
                const float inset = rect.w * 0.18f;
                const bool pressed = player_input->controls_pressed[i];
                CLAY(CLAY_IDI("HUD control", i), {
                    .layout = {
                        .sizing = {
                            CLAY_SIZING_FIXED(rect.w),
                            CLAY_SIZING_FIXED(rect.h),
                        },
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                    },
                    .backgroundColor = pressed
                            ? (Clay_Color){ 255, 255, 255, 52 }
                            : (Clay_Color){ 0, 0, 0, 96 },
                    .cornerRadius = CLAY_CORNER_RADIUS(rect.w * 0.22f),
                    .floating = {
                        .offset = {
                            rect.x - context->view.gui_safe_area.x,
                            rect.y - context->view.gui_safe_area.y,
                        },
                        .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                        .attachTo = CLAY_ATTACH_TO_ROOT,
                    },
                    .border = {
                        .color = pressed
                                ? (Clay_Color){ 255, 255, 255, 160 }
                                : (Clay_Color){ 255, 255, 255, 64 },
                        .width = CLAY_BORDER_ALL(2),
                    },
                }) {
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = {
                                CLAY_SIZING_FIXED(vkm_max(rect.w - inset * 2.0f, 1.0f)),
                                CLAY_SIZING_FIXED(vkm_max(rect.h - inset * 2.0f, 1.0f)),
                            },
                        },
                        .image = { .imageData = context->control_textures[i] },
                    }) {}
                }
            }
        }

        if (context->debug_string_builder.length) {
            CLAY(CLAY_ID("Debug text"), {
                .layout = {
                    .sizing = {
                        CLAY_SIZING_FIXED(context->view.gui_safe_area.w),
                        CLAY_SIZING_FIT(0),
                    },
                },
                .floating = {
                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                    .attachTo = CLAY_ATTACH_TO_ROOT,
                },
            }) {
                CLAY_TEXT(
                        ((Clay_String){
                            .length = (int32_t)context->debug_string_builder.length,
                            .chars = context->debug_string_builder.data,
                        }),
                        CLAY_TEXT_CONFIG({
                            .textColor = { 255, 255, 255, 255 },
                            .fontSize = 16,
                            .lineHeight = 16,
                            .wrapMode = CLAY_TEXT_WRAP_NEWLINES,
                        }));
            }
        }
    }
}

nc_gui_context_t* nc_gui_init(nc_renderer_t* renderer, nc_asset_manager_t* asset_manager) {
    nc_gui_context_t* context = calloc(1, sizeof(*context));
    context->renderer = renderer;
    nc__gui_update_view(context);

    const uint32_t clay_memory_size = Clay_MinMemorySize();
    context->clay_memory = malloc(clay_memory_size);
    Clay_Initialize(
            Clay_CreateArenaWithCapacityAndMemory(clay_memory_size, context->clay_memory),
            (Clay_Dimensions){
                context->view.gui_safe_area.w,
                context->view.gui_safe_area.h,
            },
            (Clay_ErrorHandler){ .errorHandlerFunction = nc__gui_clay_error_handler, .userData = context });
    Clay_SetMeasureTextFunction(nc__gui_measure_text, NULL);
    nc__gui_rectangle_vec_reserve(&context->rectangles, 256);
    nc__gui_command_vec_reserve(&context->commands, 64);

    uint8_t pixels[64 * 64 * 4];
    for (size_t i = 0; i < 64 * 64; i++) {
        const uint8_t value = (nc__spleen_font_bits[i / 8] & (1 << (i % 8))) ? 255 : 0;
        pixels[i * 4 + 0] = value;
        pixels[i * 4 + 1] = value;
        pixels[i * 4 + 2] = value;
        pixels[i * 4 + 3] = value;
    }
    context->font_texture = nc_renderer_create_rgba_texture_2d(
            renderer,
            false,
            64,
            64,
            pixels);
    if (!context->font_texture) {
        goto error;
    }

    for (uint32_t i = 0; i < NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        nc_texture_baked_asset_t texture_asset;
        if (!nc_asset_manager_get_texture_baked_asset(
                asset_manager,
                "novacube",
                nc__gui_control_textures[i],
                NC_TEXTURE_TYPE_GUI,
                &texture_asset)) {
            goto error;
        }
        context->control_textures[i] = nc_renderer_create_texture_from_baked_assets(
                renderer,
                &texture_asset,
                1,
                NC_TEXTURE_TYPE_GUI,
                true);
        nc_asset_manager_texture_baked_asset_fini(&texture_asset);
        if (!context->control_textures[i]) {
            goto error;
        }
    }
    nc_string_builder_init(&context->debug_string_builder);
    return context;

error:
    nc_gui_fini(context);
    return NULL;
}

const nc_gui_view_t* nc_gui_get_view(nc_gui_context_t* context) {
    nc__gui_update_view(context);
    return &context->view;
}

bool nc_gui_handle_event(nc_gui_context_t* context, const SDL_Event* event) {
    nc__gui_update_view(context);

    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (!context->captured_finger_active &&
                    (!nc_renderer_is_relative_mouse_mode(context->renderer) ||
                    context->captured_mouse_button)) {
                context->pointer_position =
                        nc__gui_window_to_clay_position(context, event->motion.x, event->motion.y);
            }
            return context->captured_mouse_button != 0;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (context->captured_finger_active ||
                    nc_renderer_is_relative_mouse_mode(context->renderer)) {
                return false;
            }
            if (context->captured_mouse_button) {
                return true;
            }
            if (!nc__gui_pointer_over_element(context, nc__gui_window_to_clay_position(
                    context,
                    event->button.x,
                    event->button.y))) {
                return false;
            }
            context->captured_mouse_button = event->button.button;
            context->pointer_position =
                    nc__gui_window_to_clay_position(context, event->button.x, event->button.y);
            context->pointer_down = true;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button != context->captured_mouse_button) {
                return false;
            }
            context->pointer_position =
                    nc__gui_window_to_clay_position(context, event->button.x, event->button.y);
            context->pointer_down = false;
            context->captured_mouse_button = 0;
            return true;
        case SDL_EVENT_MOUSE_WHEEL: {
            const Clay_Vector2 position =
                    nc__gui_window_to_clay_position(context, event->wheel.mouse_x, event->wheel.mouse_y);
            if (!nc__gui_pointer_over_element(context, position)) {
                return false;
            }
            const float direction = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
            context->pointer_position = position;
            context->scroll_delta.x += event->wheel.x * direction * NC__GUI_MOUSE_WHEEL_SCALE;
            context->scroll_delta.y += event->wheel.y * direction * NC__GUI_MOUSE_WHEEL_SCALE;
            return true;
        }
        case SDL_EVENT_FINGER_DOWN:
            if (context->captured_finger_active || context->captured_mouse_button) {
                return false;
            }
            if (!nc__gui_pointer_over_element(context, nc__gui_touch_to_clay_position(
                    context,
                    event->tfinger.x,
                    event->tfinger.y))) {
                return false;
            }
            context->captured_finger = event->tfinger.fingerID;
            context->captured_finger_active = true;
            context->pointer_position =
                    nc__gui_touch_to_clay_position(context, event->tfinger.x, event->tfinger.y);
            context->pointer_down = true;
            return true;
        case SDL_EVENT_FINGER_MOTION:
            if (!context->captured_finger_active ||
                    event->tfinger.fingerID != context->captured_finger) {
                return false;
            }
            context->pointer_position =
                    nc__gui_touch_to_clay_position(context, event->tfinger.x, event->tfinger.y);
            return true;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            if (!context->captured_finger_active ||
                    event->tfinger.fingerID != context->captured_finger) {
                return false;
            }
            context->pointer_position =
                    nc__gui_touch_to_clay_position(context, event->tfinger.x, event->tfinger.y);
            context->pointer_down = false;
            context->captured_finger_active = false;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (context->captured_finger_active || context->captured_mouse_button) {
                context->pointer_down = false;
                context->captured_finger_active = false;
                context->captured_mouse_button = 0;
            }
            return false;
        default:
            return false;
    }
}

void nc_gui_prepare_frame(
    nc_gui_context_t* context,
    const nc_player_input_overlay_t* player_input,
    const float delta_time
) {
    nc__gui_update_view(context);
    Clay_SetLayoutDimensions((Clay_Dimensions){
        context->view.gui_safe_area.w,
        context->view.gui_safe_area.h,
    });
    Clay_SetPointerState(context->pointer_position, context->pointer_down);
    // Clay owns scroll offsets and momentum; SDL wheel events only accumulate deltas here.
    Clay_UpdateScrollContainers(true, context->scroll_delta, delta_time);
    context->scroll_delta = (Clay_Vector2){ 0 };
    // The current HUD is display-only. Touch controls are laid out and hit-tested by player_input
    // because it supports multiple simultaneous fingers, while Clay accepts only one pointer.
    nc__gui_build_hud(context, player_input);
    nc__gui_build_draw_list(context, Clay_EndLayout(delta_time));
    // It's necessary to reset the cache every frame because the debug text is very dynamic and overflows it.
    Clay_ResetMeasureTextCache();
    nc_string_builder_clear(&context->debug_string_builder);
}

void nc_gui_get_overlay_draw(const nc_gui_context_t* context, nc_renderer_overlay_draw_t* draw) {
    *draw = (nc_renderer_overlay_draw_t){
        .rectangles = context->rectangles.array,
        .rectangle_count = nc__gui_rectangle_vec_count(&context->rectangles),
        .draw_commands = context->commands.array,
        .draw_command_count = nc__gui_command_vec_count(&context->commands),
        .font_texture = context->font_texture,
    };
}

void nc_gui_get_procedural_overlay_draw(
    const nc_player_input_overlay_t* player_input,
    nc_renderer_procedural_overlay_draw_t* draw
) {
    *draw = (nc_renderer_procedural_overlay_draw_t){
        .analog_stick_ring_radius = player_input->analog_stick_ring_radius,
        .analog_stick_ring_thickness = player_input->analog_stick_ring_thickness,
        .analog_stick_radius = player_input->analog_stick_radius,
        .crosshair_size = vkm_max((float)nc_cvar_get_crosshair_size(), 1.0f),
    };
    for (uint32_t i = 0; i < 2; i++) {
        draw->analog_sticks_active[i] = player_input->analog_sticks_active[i];
        draw->analog_stick_ring_positions[i] = player_input->analog_stick_ring_positions[i];
        draw->analog_stick_positions[i] = player_input->analog_stick_positions[i];
    }
}

void nc_gui_append_debug_text(nc_gui_context_t* context, const char* text, const size_t length) {
    nc_string_builder_append(&context->debug_string_builder, text, length ? length : strlen(text));
}

void nc_gui_fini(nc_gui_context_t* context) {
    if (!context) {
        return;
    }
    nc_string_builder_fini(&context->debug_string_builder);
    for (uint32_t i = 0; i < NC_PLAYER_INPUT_CONTROL_COUNT; i++) {
        if (context->control_textures[i]) {
            nc_renderer_destroy_texture(context->renderer, context->control_textures[i]);
        }
    }
    if (context->font_texture) {
        nc_renderer_destroy_texture(context->renderer, context->font_texture);
    }
    nc__gui_command_vec_fini(&context->commands);
    nc__gui_rectangle_vec_fini(&context->rectangles);
    free(context->clay_memory);
    free(context);
}
