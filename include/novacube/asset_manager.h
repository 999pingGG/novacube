#pragma once
#ifndef NOVACUBE_ASSET_MANAGER_H_
#define NOVACUBE_ASSET_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/argument_parser.h>

typedef struct nc_asset_manager_t nc_asset_manager_t;

typedef struct nc_shader_baked_asset_t {
    // Owned SPIR-V allocation. Release it with nc_asset_manager_shader_baked_asset_fini().
    void* spirv_bytecode;
    uint32_t code_size;
} nc_shader_baked_asset_t;

typedef enum nc_shader_stage_t {
    NC_SHADER_STAGE_VERTEX = 1,
    NC_SHADER_STAGE_FRAGMENT,
} nc_shader_stage_t;

typedef struct nc_texture_baked_asset_t {
    // Owned pixel allocation. Release it with nc_asset_manager_texture_baked_asset_fini().
    void* pixels;
    int16_t width;
    int16_t height;
} nc_texture_baked_asset_t;

typedef enum nc_texture_type_t {
    NC_TEXTURE_TYPE_BLOCK = 1,
    NC_TEXTURE_TYPE_GUI,
} nc_texture_type_t;

// TODO: Decouple asset baking from command-line argument parsing.
bool nc_asset_manager_bake_assets(const nc_arguments_t* arguments);
// Owns the database connection, prepared statements, and decompression context. The manager is not thread-safe.
nc_asset_manager_t* nc_asset_manager_init(void);
// On success, the caller owns the asset's allocation. On failure, the asset is zeroed.
bool nc_asset_manager_get_shader_baked_asset(
        nc_asset_manager_t* asset_manager,
        const char* namespace,
        const char* name,
        nc_shader_stage_t stage,
        nc_shader_baked_asset_t* asset);
void nc_asset_manager_shader_baked_asset_fini(nc_shader_baked_asset_t* asset);
// On success, the caller owns the asset's allocation. On failure, the asset is zeroed.
bool nc_asset_manager_get_texture_baked_asset(
        nc_asset_manager_t* asset_manager,
        const char* namespace,
        const char* name,
        nc_texture_type_t type,
        nc_texture_baked_asset_t* asset);
void nc_asset_manager_texture_baked_asset_fini(nc_texture_baked_asset_t* asset);
void nc_asset_manager_fini(nc_asset_manager_t* asset_manager);

#endif
