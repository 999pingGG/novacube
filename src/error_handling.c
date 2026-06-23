#include <stdarg.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include <novacube/error_handling.h>
#include <novacube/standard_functions.h>

void nc_set_error(const char* file, const int line, const char* format, ...) {
    if (!format) {
        // Just add the file and line to the SDL error.
        SDL_SetError("%s(%i): %s", file, line, SDL_GetError());
        return;
    }

    char message[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    const char* sdl_error = SDL_GetError();
    if (sdl_error[0]) {
        SDL_SetError("%s(%i): %s %s", file, line, message, sdl_error);
    } else {
        SDL_SetError("%s(%i): %s", file, line, message);
    }
}

void nc_show_error_dialog(void) {
    const char* message = SDL_GetError();
    NC_ASSERT(message[0]);

    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", message, NULL);

    SDL_ClearError();
}
