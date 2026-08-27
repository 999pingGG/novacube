#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <sqlite3.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <novacube/asset_manager.h>
#ifdef ANDROID
#include <novacube/android_asset_vfs.h>
#endif
#include <novacube/error_handling.h>
#include <novacube/standard_functions.h>

#ifdef ANDROID
// mobile (ASTC) format
#define NC__ASSET_MANAGER_IN_DATABASE_TEXTURE_FORMAT "1"
#define NC__ASSET_MANAGER_STRINGIFY_VALUE(value) #value
#define NC__ASSET_MANAGER_STRINGIFY(value) NC__ASSET_MANAGER_STRINGIFY_VALUE(value)
#else
// desktop (BC7) format
#define NC__ASSET_MANAGER_IN_DATABASE_TEXTURE_FORMAT "2"
#endif

#define NC__CHECK_SQLITE_RESULT(database, expression) do { \
    const int nc__sqlite_result = (expression); \
    if (nc__sqlite_result != SQLITE_OK) { \
        NC_SET_ERROR("%s", sqlite3_errmsg(database)); \
        goto error; \
    } \
} while (false)

typedef struct nc_asset_manager_t {
    sqlite3* database;
    sqlite3_stmt* get_baked_shader_asset;
    sqlite3_stmt* get_baked_texture_asset;
    ZSTD_DCtx* zstd_decompression_context;
} nc_asset_manager_t;

static bool nc__asset_manager_decompress(
    ZSTD_DCtx* decompression_context,
    const void* compressed_data,
    const size_t compressed_data_size,
    void** data,
    size_t* data_size
) {
    *data = NULL;
    *data_size = ZSTD_getFrameContentSize(compressed_data, compressed_data_size);
    if (*data_size == ZSTD_CONTENTSIZE_ERROR || *data_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        NC_SET_ERROR("Invalid compressed data.");
        return false;
    }

    *data = malloc(*data_size);

    const size_t reported_size = ZSTD_decompressDCtx(
            decompression_context,
            *data,
            *data_size,
            compressed_data,
            compressed_data_size);
    if (ZSTD_isError(reported_size)) {
        NC_SET_ERROR("Failed to decompress data: %s", ZSTD_getErrorName(reported_size));
        free(*data);
        *data = NULL;
        return false;
    }
    if (reported_size != *data_size) {
        NC_SET_ERROR("Decompressed data size mismatch: expected %zu bytes, got %zu.", *data_size, reported_size);
        free(*data);
        *data = NULL;
        return false;
    }
    return true;
}

static size_t nc__asset_manager_block_compressed_payload_size(const uint32_t width, const uint32_t height) {
    return ((size_t)width + 3) / 4 * (((size_t)height + 3) / 4) * 16;
}

static uint32_t nc__asset_manager_mip_level_count(uint32_t width, uint32_t height) {
    uint32_t largest_dimension = width > height ? width : height;
    uint32_t result = 1;
    while (largest_dimension > 1) {
        largest_dimension >>= 1;
        result++;
    }
    return result;
}

static size_t nc__asset_manager_block_compressed_mip_chain_size(
    uint32_t width,
    uint32_t height,
    const uint32_t mip_level_count
) {
    size_t result = 0;
    for (uint32_t mip_level = 0; mip_level < mip_level_count; mip_level++) {
        result += nc__asset_manager_block_compressed_payload_size(width, height);
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return result;
}

nc_asset_manager_t* nc_asset_manager_init(void) {
    nc_asset_manager_t* result = calloc(1, sizeof(*result));
    result->zstd_decompression_context = ZSTD_createDCtx();

#ifdef ANDROID
    if (!nc_android_asset_vfs_register()) {
        goto error;
    }
    const char* database_path = "assets.db";
    const char* database_vfs = NC_ANDROID_ASSET_VFS_NAME;
#else
    const char* base_path = SDL_GetBasePath();
    NC_CHECK_SDL_RESULT(base_path);
    char database_path[FILENAME_MAX];
    const int database_path_length = snprintf(database_path, sizeof(database_path), "%sassets.db", base_path);
    NC_CHECK_RESULT(
            database_path_length >= 0 && database_path_length < (int)sizeof(database_path),
            "The runtime asset database path is too long.");
    const char* database_vfs = NULL;
#endif

    NC__CHECK_SQLITE_RESULT(result->database, sqlite3_open_v2(
            database_path,
            &result->database,
            SQLITE_OPEN_READONLY,
            database_vfs));
#ifdef ANDROID
    NC__CHECK_SQLITE_RESULT(result->database, sqlite3_exec(
            result->database,
            "PRAGMA mmap_size = " NC__ASSET_MANAGER_STRINGIFY(NC_ANDROID_ASSET_MMAP_SIZE),
            NULL,
            NULL,
            NULL));
#endif

    NC__CHECK_SQLITE_RESULT(result->database, sqlite3_prepare_v2(
            result->database,
            "SELECT spirv_bytecode FROM ShaderAsset WHERE namespace = ? AND name = ? AND subtype = ?",
            -1,
            &result->get_baked_shader_asset,
            NULL));

    NC__CHECK_SQLITE_RESULT(result->database, sqlite3_prepare_v2(
            result->database,
            "SELECT "
                "width, "
                "height, "
                "pixels "
            "FROM TextureAsset "
            "WHERE "
                "namespace = ? "
                "AND name = ? "
                "AND subtype = ? "
                "AND format = " NC__ASSET_MANAGER_IN_DATABASE_TEXTURE_FORMAT,
            -1,
            &result->get_baked_texture_asset,
            NULL));

    return result;

error:
    if (result) {
        nc_asset_manager_fini(result);
    }
    return NULL;
}

bool nc_asset_manager_get_shader_baked_asset(
    nc_asset_manager_t* asset_manager,
    const char* namespace,
    const char* name,
    const nc_shader_stage_t stage,
    nc_shader_baked_asset_t* asset
) {
    *asset = (nc_shader_baked_asset_t){ 0 };
    NC__CHECK_SQLITE_RESULT(asset_manager->database, sqlite3_bind_text(
            asset_manager->get_baked_shader_asset,
            1,
            namespace,
            -1,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_manager->database, sqlite3_bind_text(
            asset_manager->get_baked_shader_asset,
            2,
            name,
            -1,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_manager->database, sqlite3_bind_int(
            asset_manager->get_baked_shader_asset,
            3,
            stage));

    int sqlite3_result = sqlite3_step(asset_manager->get_baked_shader_asset);
    NC_ASSERT(sqlite3_result == SQLITE_DONE || sqlite3_result == SQLITE_ROW);
    bool success = false;
    if (sqlite3_result == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(asset_manager->get_baked_shader_asset, 0);
        const int blob_size = sqlite3_column_bytes(asset_manager->get_baked_shader_asset, 0);
        const int spirv_header_size = 20;
        if (blob_size >= (int)sizeof(uint32_t)) {
            uint32_t magic;
            memcpy(&magic, blob, sizeof(magic));
            if (magic == 0xFD2FB528) {
                // zstd-compressed
                size_t size;
                if (!nc__asset_manager_decompress(
                        asset_manager->zstd_decompression_context,
                        blob,
                        blob_size,
                        &asset->spirv_bytecode,
                        &size)) {
                    success = false;
                } else if (size < (size_t)spirv_header_size || size % 4 != 0 || size > UINT32_MAX) {
                    NC_SET_ERROR("Invalid shader bytecode size %zu: %s:%s", size, namespace, name);
                } else {
                    uint32_t decompressed_magic;
                    memcpy(&decompressed_magic, asset->spirv_bytecode, sizeof(decompressed_magic));
                    if (decompressed_magic != 0x07230203) {
                        NC_SET_ERROR("Invalid decompressed SPIR-V magic for shader %s:%s", namespace, name);
                    } else {
                        asset->code_size = (uint32_t)size;
                        success = true;
                    }
                }
            } else if (magic == 0x07230203) {
                // uncompressed SPIR-V
                if (blob_size < spirv_header_size || blob_size % 4 != 0) {
                    NC_SET_ERROR("Invalid shader bytecode size %d: %s:%s", blob_size, namespace, name);
                } else {
                    *asset = (nc_shader_baked_asset_t){
                        .spirv_bytecode = malloc(blob_size),
                        .code_size = blob_size,
                    };
                    memcpy(asset->spirv_bytecode, blob, blob_size);
                    success = true;
                }
            } else {
                NC_SET_ERROR("Unexpected shader bytecode magic 0x%X: %s:%s", magic, namespace, name);
            }
        } else {
            NC_SET_ERROR("Invalid shader bytecode size %d: %s:%s", blob_size, namespace, name);
        }

        NC_ASSERT(sqlite3_step(asset_manager->get_baked_shader_asset) == SQLITE_DONE);
    } else {
        NC_SET_ERROR("Shader asset not found: %s:%s (stage %d)", namespace, name, stage);
    }

    NC__CHECK_SQLITE_RESULT(
            asset_manager->database,
            sqlite3_reset(asset_manager->get_baked_shader_asset));
    NC__CHECK_SQLITE_RESULT(
            asset_manager->database,
            sqlite3_clear_bindings(asset_manager->get_baked_shader_asset));

    if (!success) {
        free(asset->spirv_bytecode);
        *asset = (nc_shader_baked_asset_t){ 0 };
    }
    return success;

error:
    free(asset->spirv_bytecode);
    *asset = (nc_shader_baked_asset_t){ 0 };
    sqlite3_result = sqlite3_reset(asset_manager->get_baked_shader_asset);
    (void)sqlite3_result;
    NC_ASSERT(sqlite3_result == SQLITE_OK);
    sqlite3_result = sqlite3_clear_bindings(asset_manager->get_baked_shader_asset);
    NC_ASSERT(sqlite3_result == SQLITE_OK);
    return false;
}

void nc_asset_manager_shader_baked_asset_fini(nc_shader_baked_asset_t* asset) {
    free(asset->spirv_bytecode);
    *asset = (nc_shader_baked_asset_t){ 0 };
}

bool nc_asset_manager_get_texture_baked_asset(
    nc_asset_manager_t* asset_manager,
    const char* namespace,
    const char* name,
    nc_texture_type_t type,
    nc_texture_baked_asset_t* asset
) {
    *asset = (nc_texture_baked_asset_t){ 0 };
    void* uncompressed_data = NULL;

    NC__CHECK_SQLITE_RESULT(asset_manager->database, sqlite3_bind_text(
            asset_manager->get_baked_texture_asset,
            1,
            namespace,
            -1,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_manager->database, sqlite3_bind_text(
            asset_manager->get_baked_texture_asset,
            2,
            name,
            -1,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_manager->database, sqlite3_bind_int(
            asset_manager->get_baked_texture_asset,
            3,
            type));

    int sqlite3_result = sqlite3_step(asset_manager->get_baked_texture_asset);
    NC_ASSERT(sqlite3_result == SQLITE_DONE || sqlite3_result == SQLITE_ROW);
    bool success = false;
    if (sqlite3_result == SQLITE_ROW) {
        const int width = sqlite3_column_int(asset_manager->get_baked_texture_asset, 0);
        const int height = sqlite3_column_int(asset_manager->get_baked_texture_asset, 1);
        const void* blob = sqlite3_column_blob(asset_manager->get_baked_texture_asset, 2);
        const int blob_size = sqlite3_column_bytes(asset_manager->get_baked_texture_asset, 2);

        size_t uncompressed_data_size;
        if (!nc__asset_manager_decompress(
                asset_manager->zstd_decompression_context,
                blob,
                blob_size,
                &uncompressed_data,
                &uncompressed_data_size)) {
            success = false;
        } else {
            if (width <= 0 || height <= 0 || width > INT16_MAX || height > INT16_MAX) {
                NC_SET_ERROR(
                        "Texture %s:%s has invalid dimensions %dx%d, the maximum is %dx%d.",
                        namespace,
                        name,
                        width,
                        height,
                        INT16_MAX,
                        INT16_MAX);
            } else {
                const uint32_t mip_level_count = type == NC_TEXTURE_TYPE_BLOCK
                        ? nc__asset_manager_mip_level_count((uint32_t)width, (uint32_t)height)
                        : 1;
                const size_t expected_size = nc__asset_manager_block_compressed_mip_chain_size(
                        (uint32_t)width,
                        (uint32_t)height,
                        mip_level_count);
                if (expected_size != uncompressed_data_size) {
                    NC_SET_ERROR(
                            "Texture %s:%s has mismatched data size: expected %zu bytes, got %zu.",
                            namespace,
                            name,
                            expected_size,
                            uncompressed_data_size);
                } else {
                    *asset = (nc_texture_baked_asset_t){
                        .pixels = uncompressed_data,
                        .width = (int16_t)width,
                        .height = (int16_t)height,
                    };
                    uncompressed_data = NULL;
                    success = true;
                }
            }
        }

        NC_ASSERT(sqlite3_step(asset_manager->get_baked_texture_asset) == SQLITE_DONE);
    } else {
        NC_SET_ERROR("Texture asset not found: %s:%s (type %d)", namespace, name, type);
    }

    NC__CHECK_SQLITE_RESULT(
            asset_manager->database,
            sqlite3_reset(asset_manager->get_baked_texture_asset));
    NC__CHECK_SQLITE_RESULT(
            asset_manager->database,
            sqlite3_clear_bindings(asset_manager->get_baked_texture_asset));

    free(uncompressed_data);
    return success;

error:
    free(uncompressed_data);
    sqlite3_result = sqlite3_reset(asset_manager->get_baked_texture_asset);
    (void)sqlite3_result;
    NC_ASSERT(sqlite3_result == SQLITE_OK);
    sqlite3_result = sqlite3_clear_bindings(asset_manager->get_baked_texture_asset);
    NC_ASSERT(sqlite3_result == SQLITE_OK);
    return false;
}

void nc_asset_manager_texture_baked_asset_fini(nc_texture_baked_asset_t* asset) {
    free(asset->pixels);
    *asset = (nc_texture_baked_asset_t){ 0 };
}

void nc_asset_manager_fini(nc_asset_manager_t* asset_manager) {
    if (!asset_manager) {
        return;
    }

    ZSTD_freeDCtx(asset_manager->zstd_decompression_context);

    int sqlite3_result = sqlite3_finalize(asset_manager->get_baked_texture_asset);
    (void)sqlite3_result;
    NC_ASSERT(sqlite3_result == SQLITE_OK);

    sqlite3_result = sqlite3_finalize(asset_manager->get_baked_shader_asset);
    NC_ASSERT(sqlite3_result == SQLITE_OK);

    if (asset_manager->database) {
        sqlite3_result = sqlite3_close(asset_manager->database);
        NC_ASSERT(sqlite3_result == SQLITE_OK);
    }

    free(asset_manager);
}
