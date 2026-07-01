#pragma once
#ifndef NOVACUBE_STRING_BUILDER_H_
#define NOVACUBE_STRING_BUILDER_H_

#include <stddef.h>

typedef struct nc_string_builder_t {
    char* data;
    size_t length;
    size_t capacity;
} nc_string_builder_t;

void nc_string_builder_init(nc_string_builder_t* string_builder);
void nc_string_builder_reserve(nc_string_builder_t* string_builder, size_t additional_length);
void nc_string_builder_append(nc_string_builder_t* string_builder, const char* string, size_t length);
void nc_string_builder_clear(nc_string_builder_t* string_builder);
void nc_string_builder_fini(nc_string_builder_t* string_builder);

#endif
