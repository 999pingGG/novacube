#pragma once
#ifndef NOVACUBE_STRING_HANDLING_H_
#define NOVACUBE_STRING_HANDLING_H_

#include <stdbool.h>
#include <stddef.h>

typedef struct nc_string_builder_t {
    char* data;
    size_t length;
    size_t capacity;
} nc_string_builder_t;

typedef struct nc_string_slice_t {
    const char* start;
    size_t length;
} nc_string_slice_t;

void nc_string_builder_init(nc_string_builder_t* string_builder);
void nc_string_builder_reserve(nc_string_builder_t* string_builder, size_t additional_length);
void nc_string_builder_append(nc_string_builder_t* string_builder, const char* string, size_t length);
void nc_string_builder_clear(nc_string_builder_t* string_builder);
void nc_string_builder_fini(nc_string_builder_t* string_builder);

bool nc_string_slice_equals_string(const nc_string_slice_t* slice, const char* string);
void nc_scan_identifier(const char** current_position, nc_string_slice_t* slice);

#endif
