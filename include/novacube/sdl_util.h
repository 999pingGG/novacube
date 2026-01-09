#pragma once
#ifndef _NOVACUVE_SDL_UTIL_H_
#define _NOVACUVE_SDL_UTIL_H_
#define NC_CHECK_SDL_RESULT(result) do { \
    if (!result) { \
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", SDL_GetError(), NULL); \
        goto error; \
    } \
} while (false)
#endif
