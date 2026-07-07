#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include <novacube/build_info.h>
#include <novacube/cvar.h>
#include <novacube/error_handling.h>
#include <novacube/macros.h>
#include <novacube/standard_functions.h>
#include <novacube/string_builder.h>

#define NC__CONFIGURATION_FILE "config.ini"
#define NC__DOUBLE_DECIMAL_PLACES 13
#define NC__DOUBLE_ROUNDING_UNIT 0.0000000000001

// meh, just to relinquish "suggest braces around initialization of subobject"
NC_IGNORE_ALL_WARNINGS_BEGIN

#define X(type, name, _1, _2, _3, _4, _5, ...) \
    static type nc__##name = { __VA_ARGS__ }; \
    type nc_cvar_get_##name(void) { \
        return nc__##name; \
    } \
\
    void nc_cvar_set_##name(type new_##name) { \
        nc__##name = new_##name; \
    }
NC_CVAR_TABLE(X)
#undef X

NC_IGNORE_ALL_WARNINGS_END

#define NC__CVAR_STR_YES(name, comment, default_value_string) comment #name " = " default_value_string "\n"
#define NC__CVAR_STR_NO(name, comment, default_value_string) ""

#define NC__DEFAULT_CONFIG_FILE(_1, name, in_file_by_default, comment, _2, _3, default_value_string, ...) \
    NC__CONCAT(NC__CVAR_STR_, in_file_by_default)(name, comment, default_value_string)

#define NC_DEFAULT_CONFIG_FILE_CONTENTS NC_CVAR_TABLE(NC__DEFAULT_CONFIG_FILE)

typedef struct nc__string_slice_t {
    const char* start;
    size_t length;
} nc__string_slice_t;

typedef struct nc__config_key_value_t {
    nc__string_slice_t key;
    nc__string_slice_t value;
} nc__config_key_value_t;

typedef struct nc__enum_entry_t {
    const char* string;
    unsigned length;
    int value;
} nc__enum_entry_t;

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

static bool nc__parse_uint8(nc__string_slice_t* slice, uint8_t* result) {
    int n;
    if (!nc__parse_int(slice, &n) || n < 0 || n > UINT8_MAX) {
        return false;
    }

    *result = (uint8_t)n;
    return true;
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

static bool nc__parse_ubvec4(nc__string_slice_t* slice, vkm_ubvec4* result) {
    if (!nc__parse_uint8(slice, &result->x)) {
        return false;
    }

    nc__skip_whitespace_slice(slice);
    if (*slice->start != ',') {
        return false;
    }

    // Consume the comma and skip to the next int.
    nc__skip_char_slice(slice);
    nc__skip_whitespace_slice(slice);

    if (!nc__parse_uint8(slice, &result->y)) {
        return false;
    }

    nc__skip_whitespace_slice(slice);
    if (*slice->start != ',') {
        return false;
    }

    nc__skip_char_slice(slice);
    nc__skip_whitespace_slice(slice);

    if (!nc__parse_uint8(slice, &result->z)) {
        return false;
    }

    nc__skip_whitespace_slice(slice);
    if (*slice->start != ',') {
        return false;
    }

    nc__skip_char_slice(slice);
    nc__skip_whitespace_slice(slice);

    if (!nc__parse_uint8(slice, &result->w)) {
        return false;
    }

    // Ensure there's no trailing characters.
    return slice->length == 0;
}

static bool nc__parse_double(nc__string_slice_t* slice, double* result) {
    if (slice->length == 0) {
        return false;
    }

    *result = 0.0;
    double fractional_divisor = 1.0;
    double sign = 1.0;
    bool has_digit = false;
    bool has_dot = false;
    unsigned decimal_places = 0;

    if (*slice->start == '-') {
        sign = -1.0;
        nc__skip_char_slice(slice);
        nc__skip_whitespace_slice(slice);
    } else if (*slice->start == '+') {
        nc__skip_char_slice(slice);
        nc__skip_whitespace_slice(slice);
    }

    while (slice->length && (nc__is_digit(*slice->start) || *slice->start == '.')) {
        if (*slice->start == '.') {
            if (has_dot) {
                // Dot is allowed just once.
                return false;
            }
            has_dot = true;
            nc__skip_char_slice(slice);
            continue;
        }

        has_digit = true;

        if (has_dot) {
            decimal_places++;
            if (decimal_places <= NC__DOUBLE_DECIMAL_PLACES) {
                fractional_divisor *= 10.0;
                *result += (double)(*slice->start - '0') / fractional_divisor;
            }
        } else {
            const double digit = (double)(*slice->start - '0');
            if (*result > (DBL_MAX - digit) / 10.0) {
                return false;
            }

            *result *= 10.0;
            *result += digit;
        }

        nc__skip_char_slice(slice);
    }

    nc__skip_whitespace_slice(slice);
    if (slice->length != 0) {
        return false;
    }

    *result *= sign;

    return has_digit;
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

static void nc__print_enum(nc_string_builder_t* string_builder, const nc__enum_entry_t* entries, const int value) {
    while (entries->string) {
        if (entries->value == value) {
            nc_string_builder_append(string_builder, entries->string, entries->length);
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

static const nc__enum_entry_t nc__touch_movement_mode_entries[] = {
#define X(id, string) { string, sizeof(string) - 1, NC_TOUCH_MOVEMENT_MODE_##id },
    NC_TOUCH_MOVEMENT_MODE_TABLE(X)
#undef X
    { 0 },
};

static const nc__enum_entry_t nc__touch_camera_mode_entries[] = {
#define X(id, string) { string, sizeof(string) - 1, NC_TOUCH_CAMERA_MODE_##id },
    NC_TOUCH_CAMERA_MODE_TABLE(X)
#undef X
    { 0 },
};

static const nc__enum_entry_t nc__block_highlight_effect_entries[] = {
#define X(id, string) { string, sizeof(string) - 1, NC_BLOCK_HIGHLIGHT_EFFECT_##id },
    NC_BLOCK_HIGHLIGHT_EFFECT_TABLE(X)
#undef X
    { 0 },
};

static bool nc__parse_video_mode(nc__string_slice_t* slice, nc_video_mode_t* result) {
    return nc__parse_enum(slice, nc__video_mode_entries, (int*)result);
}

static bool nc__parse_touch_movement_mode(nc__string_slice_t* slice, nc_touch_movement_mode_t* result) {
    return nc__parse_enum(slice, nc__touch_movement_mode_entries, (int*)result);
}

static bool nc__parse_touch_camera_mode(nc__string_slice_t* slice, nc_touch_camera_mode_t* result) {
    return nc__parse_enum(slice, nc__touch_camera_mode_entries, (int*)result);
}

static bool nc__parse_block_highlight_effect(nc__string_slice_t* slice, nc_block_highlight_effect_t* result) {
    return nc__parse_enum(slice, nc__block_highlight_effect_entries, (int*)result);
}

static bool nc__parse_bool(nc__string_slice_t* slice, bool* result) {
    nc__right_trim_slice(slice);

    if (       nc__string_slice_equals_string(slice, "true")
            || nc__string_slice_equals_string(slice, "yes")
            || nc__string_slice_equals_string(slice, "please")
            || nc__string_slice_equals_string(slice, "ofc")) {
        *result = true;
        return true;
    } else if (nc__string_slice_equals_string(slice, "false")
            || nc__string_slice_equals_string(slice, "no")
            || nc__string_slice_equals_string(slice, "nope")
            || nc__string_slice_equals_string(slice, "nah")) {
        *result = false;
        return true;
    }

    return false;
}

static void nc__print_int(nc_string_builder_t* string_builder, int value) {
    if (value == 0) {
        nc_string_builder_append(string_builder, "0", 1);
        return;
    }

    char buffer[30];
    buffer[sizeof(buffer) - 1] = '\0';

    bool positive = value >= 0;
    value = positive ? value : -value;

    int i;
    for (i = sizeof(buffer) - 2; value; i--, value /= 10) {
        buffer[i] = (value % 10) + '0';
    }

    if (!positive) {
        buffer[i] = '-';
        i--;
    }
    i++;

    nc_string_builder_append(string_builder, buffer + i, sizeof(buffer) - 1 - i);
}

static void nc__print_double(nc_string_builder_t* string_builder, const double value) {
    if (value != value || value > DBL_MAX || value < -DBL_MAX) {
        NC_ASSERT(false);
        nc_string_builder_append(string_builder, "0.0", 3);
        return;
    }

    double absolute_value = value < 0.0 ? -value : value;
    absolute_value += NC__DOUBLE_ROUNDING_UNIT * 0.5;

    if (absolute_value < NC__DOUBLE_ROUNDING_UNIT) {
        nc_string_builder_append(string_builder, "0.0", 3);
        return;
    }

    if (value < 0.0) {
        nc_string_builder_append(string_builder, "-", 1);
    }

    double power = 1.0;
    while (power <= absolute_value / 10.0) {
        power *= 10.0;
    }

    while (power >= 1.0) {
        int digit = (int)(absolute_value / power);
        if (digit > 9) {
            digit = 9;
        }

        const char character = (char)digit + '0';
        nc_string_builder_append(string_builder, &character, 1);
        absolute_value -= (double)digit * power;
        power /= 10.0;
    }

    char fractional_digits[NC__DOUBLE_DECIMAL_PLACES];
    unsigned fractional_length = 0;
    for (unsigned i = 0; i < NC__DOUBLE_DECIMAL_PLACES; i++) {
        absolute_value *= 10.0;
        int digit = (int)absolute_value;
        if (digit > 9) {
            digit = 9;
        }

        fractional_digits[i] = (char)digit + '0';
        absolute_value -= (double)digit;

        if (digit != 0) {
            fractional_length = i + 1;
        }
    }

    if (fractional_length == 0) {
        nc_string_builder_append(string_builder, ".0", 2);
        return;
    }

    nc_string_builder_append(string_builder, ".", 1);
    nc_string_builder_append(string_builder, fractional_digits, fractional_length);
}

static void nc__print_usvec2(nc_string_builder_t* string_builder, const vkm_usvec2 value) {
    nc__print_int(string_builder, value.x);
    nc_string_builder_append(string_builder, ", ", 2);
    nc__print_int(string_builder, value.y);
}

static void nc__print_ubvec4(nc_string_builder_t* string_builder, const vkm_ubvec4 value) {
    nc__print_int(string_builder, value.x);
    nc_string_builder_append(string_builder, ", ", 2);
    nc__print_int(string_builder, value.y);
    nc_string_builder_append(string_builder, ", ", 2);
    nc__print_int(string_builder, value.z);
    nc_string_builder_append(string_builder, ", ", 2);
    nc__print_int(string_builder, value.w);
}

static void nc__print_video_mode(nc_string_builder_t* string_builder, const nc_video_mode_t value) {
    nc__print_enum(string_builder, nc__video_mode_entries, value);
}

static void nc__print_touch_movement_mode(nc_string_builder_t* string_builder, const nc_touch_movement_mode_t value) {
    nc__print_enum(string_builder, nc__touch_movement_mode_entries, value);
}

static void nc__print_touch_camera_mode(nc_string_builder_t* string_builder, const nc_touch_camera_mode_t value) {
    nc__print_enum(string_builder, nc__touch_camera_mode_entries, value);
}

static void nc__print_block_highlight_effect(nc_string_builder_t* string_builder, const nc_block_highlight_effect_t value) {
    nc__print_enum(string_builder, nc__block_highlight_effect_entries, value);
}

static void nc__print_bool(nc_string_builder_t* string_builder, const bool value) {
    if (value) {
        nc_string_builder_append(string_builder, "yes", 3);
    } else {
        nc_string_builder_append(string_builder, "no", 2);
    }
}

static void nc__write_configuration_value(
    const nc__config_key_value_t* key_value,
    nc_string_builder_t* string_builder
) {
#define X(type, name, _1, _2, _3, print_function, ...) \
    if (nc__string_slice_equals_string(&key_value->key, #name)) { \
        print_function(string_builder, nc_cvar_get_##name()); \
        return; \
    } else
    NC_CVAR_TABLE(X) {
        nc_string_builder_append(string_builder, key_value->value.start, key_value->value.length);
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
            NC_CVAR_TABLE(NC__CONFIG_CASE);
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
    nc_string_builder_t string_builder = { 0 };
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
    nc_string_builder_init(&string_builder);

    nc__config_key_value_t key_value;
    while (nc__scan_configuration_line(&current_position, &key_value)) {
        nc_string_builder_append(
                &string_builder,
                next_unmodified_position,
                key_value.value.start - next_unmodified_position);
        nc__write_configuration_value(&key_value, &string_builder);

        nc__right_trim_slice(&key_value.value);
        next_unmodified_position = key_value.value.start + key_value.value.length;
    }

    nc_string_builder_append(&string_builder, next_unmodified_position, strlen(next_unmodified_position));

    // TODO: Add the configurations that aren't in the file if they're currently different from the default.

    const bool sdl_result = SDL_SaveFile(path, string_builder.data, string_builder.length);
    NC_CHECK_SDL_RESULT(sdl_result);

    result = true;

error:
    nc_string_builder_fini(&string_builder);
    SDL_free(file_contents);
    return result;
}
