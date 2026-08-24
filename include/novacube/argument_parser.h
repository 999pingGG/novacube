#pragma once
#ifndef NOVACUBE_ARGUMENT_PARSER_H_
#define NOVACUBE_ARGUMENT_PARSER_H_

#include <stdbool.h>

typedef enum nc_argument_platform_t {
    NC_ARGUMENT_PLATFORM_DESKTOP = 1,
    NC_ARGUMENT_PLATFORM_MOBILE,
} nc_argument_platform_t;

typedef enum nc_argument_action_t {
    NC_ARGUMENT_ACTION_RUN_GAME = 1,
    NC_ARGUMENT_ACTION_BUILD_ASSETS,
    NC_ARGUMENT_ACTION_PRINT_HELP,
    NC_ARGUMENT_ACTION_PRINT_VERSION,
} nc_argument_action_t;

typedef struct nc_arguments_t {
    char* source_assets_directory;
    char* output_database_file;
    char* texconv_executable;
    char* astcenc_executable;
    char** assets_to_build;
    int assets_to_build_count;
    nc_argument_action_t action;
    nc_argument_platform_t platform;
    bool debug;
    bool strip_png_metadata;
} nc_arguments_t;

bool nc_argument_parser_parse(char** argv, nc_arguments_t* result);

#endif
