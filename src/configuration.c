#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include <novacube/build_info.h>
#include <novacube/configuration.h>
#include <novacube/error_handling.h>
#include <novacube/macros.h>
#include <novacube/standard_functions.h>

#define NC__CONFIGURATION_FILE "config.ini"

// meh, just to relinquish "suggest braces around initialization of subobject"
NC_IGNORE_ALL_WARNINGS_START

#define X(type, name, _1, _2, _3, _4, ...) \
    static type nc__##name = { __VA_ARGS__ }; \
    type nc_config_get_##name(void) { \
        return nc__##name; \
    } \
\
    void nc_config_set_##name(type new_##name) { \
        nc__##name = new_##name; \
    }
NC_CONFIG_TABLE(X)
#undef X

NC_IGNORE_ALL_WARNINGS_END

#define NC__CONFIG_STR_YES(name, comment, ...) comment #name " = " #__VA_ARGS__ "\n"
#define NC__CONFIG_STR_NO(name, comment, ...) ""

#define NC__DEFAULT_CONFIG_FILE(_1, name, in_file_by_default, comment, _2, _3, ...) \
    NC__CONCAT(NC__CONFIG_STR_, in_file_by_default)(name, comment, __VA_ARGS__)

#define NC_DEFAULT_CONFIG_FILE_CONTENTS NC_CONFIG_TABLE(NC__DEFAULT_CONFIG_FILE)

typedef struct nc__string_slice_t {
    const char* start;
    size_t length;
} nc__string_slice_t;

typedef struct nc__config_key_value_t {
    nc__string_slice_t key;
    nc__string_slice_t value;
} nc__config_key_value_t;

typedef struct nc__string_builder_t {
    char* data;
    size_t length;
    size_t capacity;
} nc__string_builder_t;

typedef struct nc__enum_entry_t {
    const char* string;
    unsigned length;
    int value;
} nc__enum_entry_t;

static void nc__string_builder_append(nc__string_builder_t* string_builder, const char* string, size_t length);

static bool nc__is_whitespace(const char character) {
    return  character == ' '
         || character == '\t'
         || character == '\n'
         || character == '\r'
         || character == '\v'
         || character == '\f';
}

static bool nc__is_whitespace_no_line_breaks(const char character) {
    return  character == ' '
         || character == '\t'
         || character == '\v'
         || character == '\f';
}

static bool nc__is_digit(const char character) {
    return character >= '0' && character <= '9';
}

static void nc__skip_whitespace_and_line_breaks(const char** current_position) {
    while (nc__is_whitespace(**current_position)) {
        (*current_position)++;
    }
}

static void nc__skip_whitespace_and_line_breaks_inverse(const char* initial_position, const char** end) {
    while (*end > initial_position && nc__is_whitespace((*end)[-1])) {
        (*end)--;
    }
}

static void nc__skip_whitespace(const char** current_position) {
    while (nc__is_whitespace_no_line_breaks(**current_position)) {
        (*current_position)++;
    }
}

static bool nc__string_slice_equals_string(const nc__string_slice_t* slice, const char* string) {
    const size_t length = strlen(string);
    return slice->length == length && strncmp(slice->start, string, length) == 0;
}

static void nc__skip_char_slice(nc__string_slice_t* slice) {
    slice->start++;
    slice->length--;
}

static void nc__skip_whitespace_slice(nc__string_slice_t* slice) {
    while (slice->length && nc__is_whitespace_no_line_breaks(*slice->start)) {
        nc__skip_char_slice(slice);
    }
}

static void nc__skip_line_break(const char** current_position) {
    if (**current_position == '\r') {
        (*current_position)++;
    }

    if (**current_position == '\n') {
        (*current_position)++;
    }
}

static void nc__skip_line(const char** current_position) {
    while (**current_position != '\0' && **current_position != '\r' && **current_position != '\n') {
        (*current_position)++;
    }

    nc__skip_line_break(current_position);
}

static void nc__skip_junk(const char** current_position) {
    while (true) {
        nc__skip_whitespace_and_line_breaks(current_position);

        if (**current_position != ';') {
            break;
        }

        (*current_position)++;
        nc__skip_line(current_position);
    }
}

static void nc__scan_identifier(const char** current_position, nc__string_slice_t* slice) {
    *slice = (nc__string_slice_t){
        .start = *current_position,
        .length = 0,
    };

    while (     **current_position == '_'
            || (**current_position >= 'A' && **current_position <= 'Z')
            || (**current_position >= 'a' && **current_position <= 'z')
            || (slice->length > 0 && (**current_position >= '0' && **current_position <= '9'))) {
        slice->length++;
        (*current_position)++;
    }
}

static void nc__scan_configuration_value(const char** current_position, nc__string_slice_t* slice) {
    *slice = (nc__string_slice_t){
        .start = *current_position,
        .length = 0,
    };

    while (    **current_position != '\0'
            && **current_position != ';'
            && **current_position != '\r'
            && **current_position != '\n') {
        (*current_position)++;
        slice->length++;
    }
}

static void nc__right_trim_slice(nc__string_slice_t* slice) {
    const char* end = slice->start + slice->length;
    nc__skip_whitespace_and_line_breaks_inverse(slice->start, &end);
    slice->length = end - slice->start;
}

static bool nc__scan_configuration_line(const char** current_position, nc__config_key_value_t* key_value) {
    while (true) {
        nc__skip_junk(current_position);
        if (**current_position == '\0') {
            // End of the file reached, couldn't read a configuration line.
            return false;
        }

        nc__scan_identifier(current_position, &key_value->key);
        if (key_value->key.length == 0) {
            // Failed to read a valid identifier, carry on to the next line.
            nc__skip_line(current_position);
            continue;
        }

        nc__skip_whitespace(current_position);
        if (**current_position != '=') {
            // Expected an equal sign, try again with the next line.
            nc__skip_line(current_position);
            continue;
        }

        (*current_position)++;

        nc__skip_whitespace(current_position);
        nc__scan_configuration_value(current_position, &key_value->value);
        if (key_value->value.length > 0) {
            // A configuration value was successfully read, report success.
            return true;
        }

        // Failed to read a configuration value, repeat with the next line.
    }
}

static bool nc__parse_int(nc__string_slice_t* slice, int* result) {
    if (slice->length == 0) {
        return false;
    }

    *result = 0;
    int sign = 1;
    bool has_digit = false;

    if (*slice->start == '-') {
        sign = -1;
        nc__skip_char_slice(slice);
        nc__skip_whitespace_slice(slice);
    } else if (*slice->start == '+') {
        nc__skip_char_slice(slice);
        nc__skip_whitespace_slice(slice);
    }

    while (slice->length && nc__is_digit(*slice->start)) {
        has_digit = true;

        const int new_value = *result * 10;
        if (new_value < *result) {
            // Detect overflow.
            return false;
        }

        *result = new_value;
        *result += *slice->start - '0';

        nc__skip_char_slice(slice);
    }

    *result *= sign;

    return has_digit;
}

static bool nc__parse_uint16(nc__string_slice_t* slice, uint16_t* result) {
    int n;
    if (!nc__parse_int(slice, &n) || n < 0 || n > UINT16_MAX) {
        return false;
    }

    *result = (uint16_t)n;
    return true;
}

static bool nc__parse_usvec2(nc__string_slice_t* slice, vkm_usvec2* result) {
    if (!nc__parse_uint16(slice, &result->x)) {
        return false;
    }

    nc__skip_whitespace_slice(slice);
    if (*slice->start != ',') {
        return false;
    }

    // Consume the comma and skip to the next int.
    nc__skip_char_slice(slice);
    nc__skip_whitespace_slice(slice);

    if (!nc__parse_uint16(slice, &result->y)) {
        return false;
    }

    nc__skip_whitespace_slice(slice);

    // Ensure there's no trailing characters.
    return slice->length == 0;
}

static bool nc__parse_enum(nc__string_slice_t* slice, const nc__enum_entry_t* entries, int* result) {
    nc__right_trim_slice(slice);

    while (entries->string) {
        if (slice->length == entries->length && strncmp(slice->start, entries->string, entries->length) == 0) {
            *result = entries->value;
            return true;
        }

        entries++;
    }

    return false;
}

static void nc__print_enum(nc__string_builder_t* string_builder, const nc__enum_entry_t* entries, const int value) {
    while (entries->string) {
        if (entries->value == value) {
            nc__string_builder_append(string_builder, entries->string, entries->length);
            return;
        }

        entries++;
    }

    NC_ASSERT(false);
}

static const nc__enum_entry_t nc__video_mode_entries[] = {
#define X(id, string) { string, sizeof(string) - 1, NC_VIDEO_MODE_##id },
    NC_VIDEO_MODE_TABLE(X)
#undef X
    { 0 },
};

static bool nc__parse_video_mode(nc__string_slice_t* slice, nc_video_mode_t* result) {
    return nc__parse_enum(slice, nc__video_mode_entries, (int*)result);
}

static void nc__string_builder_reserve(nc__string_builder_t* string_builder, const size_t additional_length) {
    const size_t minimum_capacity = string_builder->length + additional_length + 1;
    if (minimum_capacity <= string_builder->capacity) {
        return;
    }

    size_t new_capacity = string_builder->capacity ? string_builder->capacity : 64;
    while (new_capacity < minimum_capacity) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = minimum_capacity;
            break;
        }

        new_capacity *= 2;
    }

    string_builder->data = realloc(string_builder->data, new_capacity);
    string_builder->capacity = new_capacity;
}

static void nc__string_builder_append(nc__string_builder_t* string_builder, const char* string, const size_t length) {
    nc__string_builder_reserve(string_builder, length);

    NC_MEMCPY(string_builder->data + string_builder->length, string, length);
    string_builder->length += length;
    string_builder->data[string_builder->length] = '\0';
}

static void nc__string_builder_append_string(nc__string_builder_t* string_builder, const char* string) {
    nc__string_builder_append(string_builder, string, strlen(string));
}

static void nc__print_int(nc__string_builder_t* string_builder, int value) {
    if (value == 0) {
        nc__string_builder_append(string_builder, "0", 1);
        return;
    }

    char buffer[30];
    buffer[sizeof(buffer) - 1] = '\0';

    bool positive = value >= 0;
    value = positive ? value : -value;

    int i;
    for (i = sizeof(buffer) - 2; value; i--) {
        buffer[i] = (value % 10) + '0';
        value /= 10;
    }

    if (!positive) {
        buffer[i] = '-';
        i--;
    }
    i++;

    nc__string_builder_append(string_builder, buffer + i, sizeof(buffer) - 1 - i);
}

static void nc__print_usvec2(nc__string_builder_t* string_builder, const vkm_usvec2 value) {
    nc__print_int(string_builder, value.x);
    nc__string_builder_append(string_builder, ", ", 2);
    nc__print_int(string_builder, value.y);
}

static void nc__print_video_mode(nc__string_builder_t* string_builder, const nc_video_mode_t value) {
    nc__print_enum(string_builder, nc__video_mode_entries, value);
}

static void nc__write_configuration_value(
    const nc__config_key_value_t* key_value,
    nc__string_builder_t* string_builder
) {
#define X(type, name, _1, _2, _3, print_function, ...) \
    if (nc__string_slice_equals_string(&key_value->key, #name)) { \
        print_function(string_builder, nc_config_get_##name()); \
        return; \
    } else
    NC_CONFIG_TABLE(X) {
        nc__string_builder_append(string_builder, key_value->value.start, key_value->value.length);
    }
#undef X
}

static char* nc__configuration_load_file(char out_path[FILENAME_MAX]) {
    out_path[0] = '\0';

#ifdef ANDROID
    const char* storage_path = SDL_GetAndroidExternalStoragePath();
    NC_CHECK_SDL_RESULT(storage_path);

    const int printed_length = snprintf(out_path, FILENAME_MAX, "%s/%s", storage_path, NC__CONFIGURATION_FILE);
#else
    char* pref_path = SDL_GetPrefPath(NC_COMPANY_NAME, NC_PRODUCT_NAME);
    NC_CHECK_SDL_RESULT(pref_path);

    const int printed_length = snprintf(out_path, FILENAME_MAX, "%s%s", pref_path, NC__CONFIGURATION_FILE);
    SDL_free(pref_path);
#endif

    if (printed_length < 0 || printed_length >= FILENAME_MAX) {
        NC_SET_ERROR("The configuration file path is too long.");
        goto error;
    }

    return SDL_LoadFile(out_path, NULL);

error:
    return NULL;
}

bool nc_configuration_load(void) {
    char path[FILENAME_MAX];
    char* file_contents = nc__configuration_load_file(path);
    if (file_contents) {
        // Load existing configuration.
        const char* current_position = file_contents;

        nc__config_key_value_t key_value;
        while (nc__scan_configuration_line(&current_position, &key_value)) {
#define NC__CONFIG_CASE(type, name, _1, _2, parse_function, ...) \
    if (nc__string_slice_equals_string(&key_value.key, #name)) { \
        type parsed_value; \
        nc__string_slice_t value = key_value.value; \
        if (parse_function(&value, &parsed_value)) { \
            nc__##name = parsed_value; \
        } \
    } else
            NC_CONFIG_TABLE(NC__CONFIG_CASE);
#undef NC__CONFIG_CASE
        }

        SDL_free(file_contents);
        return true;
    }

    if (path[0]) {
        // Got config file path successfully; save default configuration.
        return SDL_SaveFile(
                path,
                NC_DEFAULT_CONFIG_FILE_CONTENTS,
                sizeof(NC_DEFAULT_CONFIG_FILE_CONTENTS) - 1);
    }

    // Couldn't even get the full file path.
    return false;
}

bool nc_configuration_save(void) {
    char path[FILENAME_MAX];
    char* file_contents = nc__configuration_load_file(path);
    nc__string_builder_t string_builder = { 0 };
    bool result = false;

    if (!path[0]) {
        return false;
    }

    if (!file_contents) {
        // Couldn't open the file for some reason. Maybe it has been removed while the game was running.
        // For simplicity, generate it with the default configuration so we can patch it with the same code path.
        if (!SDL_SaveFile(
                path,
                NC_DEFAULT_CONFIG_FILE_CONTENTS,
                sizeof(NC_DEFAULT_CONFIG_FILE_CONTENTS) - 1)) {
            return false;
        }

        file_contents = SDL_LoadFile(path, NULL);
        NC_CHECK_SDL_RESULT(file_contents);
    }

    // Patch existing configuration.
    const char* current_position = file_contents;
    const char* next_unmodified_position = file_contents;

    nc__config_key_value_t key_value;
    while (nc__scan_configuration_line(&current_position, &key_value)) {
        nc__string_builder_append(
                &string_builder,
                next_unmodified_position,
                key_value.value.start - next_unmodified_position);
        nc__write_configuration_value(&key_value, &string_builder);

        nc__right_trim_slice(&key_value.value);
        next_unmodified_position = key_value.value.start + key_value.value.length;
    }

    nc__string_builder_append_string(&string_builder, next_unmodified_position);
    const bool sdl_result = SDL_SaveFile(path, string_builder.data, string_builder.length);
    NC_CHECK_SDL_RESULT(sdl_result);

    result = true;

error:
    free(string_builder.data);
    SDL_free(file_contents);
    return result;
}
