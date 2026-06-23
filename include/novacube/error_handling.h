#pragma once
#ifndef NOVACUBE_ERROR_HANDLING_H_
#define NOVACUBE_ERROR_HANDLING_H_

#define NC_SET_ERROR(...) nc_set_error(__FILE__, __LINE__, ##__VA_ARGS__)

#define NC_CHECK_RESULT(result, ...) do { \
    if (!(result)) { \
        nc_set_error(__FILE__, __LINE__, ##__VA_ARGS__); \
        goto error; \
    } \
} while (false)

#define NC_CHECK_SDL_RESULT(result) do { \
    if (!(result)) { \
        nc_set_error(__FILE__, __LINE__, NULL); \
        goto error; \
    } \
} while (false)

void nc_set_error(const char* file, int line, const char* format, ...);
void nc_show_error_dialog(void);
#endif
