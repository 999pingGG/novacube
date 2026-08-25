#pragma once
#ifndef NOVACUBE_ASSET_TYPES_H_
#define NOVACUBE_ASSET_TYPES_H_

// These values are persisted as subtype identifiers in the asset database.
typedef enum nc_shader_stage_t {
    NC_SHADER_STAGE_VERTEX = 1,
    NC_SHADER_STAGE_FRAGMENT = 2,
} nc_shader_stage_t;

// These values are persisted as subtype identifiers in the asset database.
typedef enum nc_texture_type_t {
    NC_TEXTURE_TYPE_BLOCK = 1,
    NC_TEXTURE_TYPE_GUI = 2,
} nc_texture_type_t;

#endif
