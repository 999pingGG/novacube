#include <stdint.h>
#include <stdlib.h>

#include <novacube/standard_functions.h>
#include <novacube/string_builder.h>

void nc_string_builder_init(nc_string_builder_t* string_builder) {
    *string_builder = (nc_string_builder_t){ 0 };
}

void nc_string_builder_reserve(nc_string_builder_t* string_builder, const size_t additional_length) {
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

void nc_string_builder_append(nc_string_builder_t* string_builder, const char* string, const size_t length) {
    nc_string_builder_reserve(string_builder, length);

    NC_MEMCPY(string_builder->data + string_builder->length, string, length);
    string_builder->length += length;
    string_builder->data[string_builder->length] = '\0';
}

void nc_string_builder_clear(nc_string_builder_t* string_builder) {
    string_builder->length = 0;
    string_builder->data[0] = '\0';
}

void nc_string_builder_fini(nc_string_builder_t* string_builder) {
    free(string_builder->data);
    *string_builder = (nc_string_builder_t){ 0 };
}
