#pragma once
#ifndef NOVACUBE_ASSET_BAKER_H_
#define NOVACUBE_ASSET_BAKER_H_

#include <stdbool.h>

typedef enum nc_asset_baker_platform_t {
    NC_ASSET_BAKER_PLATFORM_DESKTOP = 1,
    NC_ASSET_BAKER_PLATFORM_MOBILE,
} nc_asset_baker_platform_t;

typedef struct nc_asset_baker_options_t {
    const char* source_assets_directory;
    const char* output_database_file;
    const char* texconv_executable;
    const char* astcenc_executable;
    // The baker normalizes directory separators in these borrowed paths in place.
    char** assets_to_build;
    int assets_to_build_count;
    nc_asset_baker_platform_t platform;
    bool debug;
    bool strip_png_metadata;
} nc_asset_baker_options_t;

bool nc_asset_baker_bake_assets(const nc_asset_baker_options_t* options);

#endif
