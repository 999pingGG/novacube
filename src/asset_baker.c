#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <sqlite3.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <novacube/asset_baker.h>
#include <novacube/asset_types.h>
#include <novacube/error_handling.h>
#include <novacube/standard_functions.h>
#include <novacube/string_handling.h>

enum {
    NC__ASSET_BAKER_ZSTD_COMPRESSION_LEVEL = 22,
};

#define NC__ASSET_BAKER_TEMPORARY_TEXTURE_PREFIX "novacube-tmp-"

#define NC__CHECK_SQLITE_RESULT(database, expression) do { \
    const int nc__sqlite_result = (expression); \
    if (nc__sqlite_result != SQLITE_OK) { \
        NC_SET_ERROR("%s", sqlite3_errmsg(database)); \
        goto error; \
    } \
} while (false)

#define NC__CHECK_SQLITE_STEP(database, expression) do { \
    const int nc__sqlite_result = (expression); \
    if (nc__sqlite_result != SQLITE_DONE) { \
        NC_SET_ERROR("%s", sqlite3_errmsg(database)); \
        goto error; \
    } \
} while (false)

typedef enum nc__asset_baker_asset_type_t {
    NC_ASSET_BAKER_ASSET_TYPE_INVALID,
    NC_ASSET_BAKER_ASSET_TYPE_SHADER,
    NC_ASSET_BAKER_ASSET_TYPE_TEXTURE,

    NC_ASSET_BAKER_ASSET_TYPE_COUNT,
} nc__asset_baker_asset_type_t;

typedef struct nc__asset_baker_database_context_t {
    sqlite3* output_database;
    sqlite3_stmt* insert_shader_asset;
    sqlite3_stmt* insert_texture_asset;
    sqlite3_stmt* delete_shader_asset;
    sqlite3_stmt* delete_texture_asset;
} nc__asset_baker_database_context_t;

typedef struct nc__asset_baker_source_asset_info_t {
    // These slices borrow storage from the baker options or active SDL callback. Consumers must not retain them.
    nc_string_slice_t mod_namespace;
    nc_string_slice_t asset_name;
    const char* file_path;
    const char* output_database_file;
    const char* texconv_executable;
    const char* astcenc_executable;
    nc__asset_baker_asset_type_t asset_type;
    union {
        nc_shader_stage_t shader_stage;
        nc_texture_type_t texture_type;
    };
    bool debug;
    bool mobile;
    bool strip_png_metadata;
    ZSTD_CCtx* zstd_compression_context;
    nc__asset_baker_database_context_t database_context;
} nc__asset_baker_source_asset_info_t;

typedef struct nc__asset_baker_astc_header_t {
    uint8_t magic[4];
    uint8_t block_x;
    uint8_t block_y;
    uint8_t block_z;
    uint8_t dim_x[3];
    uint8_t dim_y[3];
    uint8_t dim_z[3];
} nc__asset_baker_astc_header_t;

static bool nc__asset_baker_execute_process_capture_stdout(
    const char** arguments,
    void** output,
    size_t* output_size
) {
    bool success = false;
    SDL_Process* process = NULL;
    *output = NULL;
    *output_size = 0;

    const SDL_PropertiesID props = SDL_CreateProperties();
    NC_CHECK_SDL_RESULT(props);
    NC_CHECK_SDL_RESULT(SDL_SetPointerProperty(
            props,
            SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
            (void*)arguments));
    NC_CHECK_SDL_RESULT(SDL_SetNumberProperty(
            props,
            SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
            SDL_PROCESS_STDIO_APP));
    process = SDL_CreateProcessWithProperties(props);
    NC_CHECK_SDL_RESULT(process);

    int exit_code;
    *output = SDL_ReadProcess(process, output_size, &exit_code);
    NC_CHECK_SDL_RESULT(*output);
    if (exit_code != 0) {
        NC_SET_ERROR("Process exited with code %i.", exit_code);
        goto error;
    }

    success = true;

error:
    if (process) {
        SDL_DestroyProcess(process);
    }
    if (props) {
        SDL_DestroyProperties(props);
    }
    return success;
}

static bool nc__asset_baker_execute_process(const char* const* arguments) {
    bool success = false;
    SDL_Process* process = SDL_CreateProcess(arguments, false);
    NC_CHECK_SDL_RESULT(process);

    int exit_code;
    NC_CHECK_SDL_RESULT(SDL_WaitProcess(process, true, &exit_code));
    if (exit_code != 0) {
        NC_SET_ERROR("Process exited with code %i.", exit_code);
        goto error;
    }

    success = true;

error:
    if (process) {
        SDL_DestroyProcess(process);
    }
    return success;
}

// Best-effort cleanup must not replace the actual baking error with a secondary missing-file or removal error.
static void nc__asset_baker_remove_temporary_file_preserving_error(const char* file_path) {
    char* original_error = strdup(SDL_GetError());
    if (SDL_GetPathInfo(file_path, NULL)) {
        if (!SDL_RemovePath(file_path)) {
            SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to remove temporary file %s: %s",
                    file_path,
                    SDL_GetError());
        }
    } else {
        SDL_ClearError();
    }

    if (original_error[0]) {
        SDL_SetError("%s", original_error);
    } else {
        SDL_ClearError();
    }
    free(original_error);
}

static bool nc__asset_baker_compress(
    ZSTD_CCtx* compression_context,
    const void* data,
    const size_t data_size,
    void** compressed_data,
    size_t* compressed_data_size
) {
    bool success = true;
    *compressed_data = NULL;

    const size_t capacity = ZSTD_COMPRESSBOUND(data_size);
    *compressed_data = malloc(capacity);
    *compressed_data_size = ZSTD_compressCCtx(
            compression_context,
            *compressed_data,
            capacity,
            data,
            data_size,
            NC__ASSET_BAKER_ZSTD_COMPRESSION_LEVEL);
    if (ZSTD_isError(*compressed_data_size)) {
        success = false;
        NC_SET_ERROR("Failed to compress data: %s", ZSTD_getErrorName(*compressed_data_size));
    }

    if (!success) {
        free(*compressed_data);
        *compressed_data = NULL;
    }
    return success;
}

static bool nc__asset_baker_bake_shader_asset(const nc__asset_baker_source_asset_info_t* asset_info) {
    bool success = false;
    void* compiler_output = NULL;
    void* compressed = NULL;

    const char* parameters[] = {
        "glslc",
        "--target-env=vulkan1.1",
        asset_info->file_path,
        "-o",
        "-",
        NULL,
        NULL,
        NULL,
    };
    if (asset_info->debug) {
        parameters[5] = "-g";
        parameters[6] = "-O0";
    } else {
        parameters[5] = "-O";
    }

    size_t compiler_output_size;
    if (!nc__asset_baker_execute_process_capture_stdout(parameters, &compiler_output, &compiler_output_size)) {
        goto error;
    }

    size_t compressed_size;
    if (!nc__asset_baker_compress(
            asset_info->zstd_compression_context,
            compiler_output,
            compiler_output_size,
            &compressed,
            &compressed_size)) {
        goto error;
    }

    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_text(
            asset_info->database_context.insert_shader_asset,
            1,
            asset_info->mod_namespace.start,
            (int)asset_info->mod_namespace.length,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_text(
            asset_info->database_context.insert_shader_asset,
            2,
            asset_info->asset_name.start,
            (int)asset_info->asset_name.length,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_int(
            asset_info->database_context.insert_shader_asset,
            3,
            asset_info->shader_stage));
    const bool should_store_uncompressed = compiler_output_size < compressed_size;
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_blob(
            asset_info->database_context.insert_shader_asset,
            4,
            should_store_uncompressed ? compiler_output : compressed,
            should_store_uncompressed ? (int)compiler_output_size : (int)compressed_size,
            SQLITE_STATIC));

    NC__CHECK_SQLITE_STEP(
            asset_info->database_context.output_database,
            sqlite3_step(asset_info->database_context.insert_shader_asset));

    NC__CHECK_SQLITE_RESULT(
            asset_info->database_context.output_database,
            sqlite3_reset(asset_info->database_context.insert_shader_asset));
    NC__CHECK_SQLITE_RESULT(
            asset_info->database_context.output_database,
            sqlite3_clear_bindings(asset_info->database_context.insert_shader_asset));

    SDL_Log("Baked shader asset %s", asset_info->file_path);
    success = true;

error:
    if (!success) {
        // SQLITE_STATIC bindings must be released before their backing allocations or borrowed slices expire.
        sqlite3_reset(asset_info->database_context.insert_shader_asset);
        sqlite3_clear_bindings(asset_info->database_context.insert_shader_asset);
    }
    free(compressed);
    SDL_free(compiler_output);
    return success;
}

static uint32_t nc__asset_baker_read_u24(const uint8_t bytes[3]) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16;
}

static uint32_t nc__asset_baker_read_u32_le(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0]
            | (uint32_t)bytes[1] << 8
            | (uint32_t)bytes[2] << 16
            | (uint32_t)bytes[3] << 24;
}

static uint32_t nc__asset_baker_read_u32_be(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0] << 24
            | (uint32_t)bytes[1] << 16
            | (uint32_t)bytes[2] << 8
            | (uint32_t)bytes[3];
}

static bool nc__asset_baker_is_png_chunk_type_byte(const uint8_t value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static bool nc__asset_baker_strip_png_metadata(const char* file_path) {
    static const uint64_t png_signature = 0xA1A0A0D474E5089;
    static const char temporary_suffix[] = ".novacube.tmp";
    bool success = false;
    bool temporary_file_may_exist = false;
    void* file_data = NULL;
    uint8_t* stripped_data = NULL;
    char* temporary_file = NULL;

    size_t file_size;
    file_data = SDL_LoadFile(file_path, &file_size);
    NC_CHECK_SDL_RESULT(file_data);
    if (file_size < sizeof(png_signature) || *(uint64_t*)file_data != png_signature) {
        NC_SET_ERROR("Texture %s does not have a valid PNG signature.", file_path);
        goto error;
    }

    stripped_data = malloc(file_size);
    *(uint64_t*)stripped_data = png_signature;
    size_t input_offset = sizeof(png_signature);
    size_t output_size = sizeof(png_signature);
    bool saw_ihdr = false;
    bool saw_iend = false;
    while (input_offset < file_size) {
        if (file_size - input_offset < 12) {
            NC_SET_ERROR("PNG %s ends in an incomplete chunk. Chunk offset: %zu", file_path, input_offset);
            goto error;
        }

        const uint8_t* chunk = (const uint8_t*)file_data + input_offset;
        const size_t chunk_data_size = nc__asset_baker_read_u32_be(chunk);
        if (chunk_data_size > file_size - input_offset - 12) {
            NC_SET_ERROR(
                    "PNG %s contains a chunk that extends past the end of the file. Chunk offset: %zu",
                    file_path,
                    input_offset);
            goto error;
        }

        const uint8_t* chunk_type = chunk + 4;
        for (int i = 0; i < 4; i++) {
            if (!nc__asset_baker_is_png_chunk_type_byte(chunk_type[i])) {
                NC_SET_ERROR("PNG %s contains an invalid chunk type. Chunk offset: %zu", file_path, input_offset);
                goto error;
            }
        }

        const size_t chunk_size = chunk_data_size + 12;
        const bool is_ihdr = *(const uint32_t*)chunk_type == 0x52444849;  // IHDR
        const bool is_iend = *(const uint32_t*)chunk_type == 0x444E4549;  // IEND
        if (!saw_ihdr) {
            if (!is_ihdr || chunk_data_size != 13) {
                NC_SET_ERROR("PNG %s does not begin with a valid IHDR chunk.", file_path);
                goto error;
            }
            saw_ihdr = true;
        } else if (is_ihdr) {
            NC_SET_ERROR(
                    "PNG %s contains more than one IHDR chunk. Second IHDR chunk offset: %zu",
                    file_path,
                    input_offset);
            goto error;
        }
        if (is_iend && chunk_data_size != 0) {
            NC_SET_ERROR("PNG %s contains an invalid IEND chunk. Chunk offset: %zu", file_path, input_offset);
            goto error;
        }

        // Detect whether the first letter of the type is uppercase ASCII.
        // If so, then the chunk is essential for decoding; keep it.
        // tRNS is the only ancillary chunk we're keeping, for transparency.
        const bool is_critical = (chunk_type[0] & 0x20) == 0;
        const bool affects_transparency = *(uint32_t*)chunk_type == 0x534E5274; // tRNS
        if (is_critical || affects_transparency) {
            memcpy(stripped_data + output_size, chunk, chunk_size);
            output_size += chunk_size;
        }

        input_offset += chunk_size;
        if (is_iend) {
            saw_iend = true;
            break;
        }
    }

    if (!saw_iend || input_offset != file_size) {
        NC_SET_ERROR("PNG %s does not end with a valid IEND chunk.", file_path);
        goto error;
    }
    if (output_size == file_size) {
        // Nothing has been stripped.
        success = true;
        goto error;
    }

    const size_t temporary_file_size = strlen(file_path) + sizeof(temporary_suffix);
    temporary_file = malloc(temporary_file_size);
    snprintf(temporary_file, temporary_file_size, "%s%s", file_path, temporary_suffix);
    if (SDL_GetPathInfo(temporary_file, NULL)) {
        NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_file));
    } else {
        SDL_ClearError();
    }

    temporary_file_may_exist = true;
    NC_CHECK_SDL_RESULT(SDL_SaveFile(temporary_file, stripped_data, output_size));
    NC_CHECK_SDL_RESULT(SDL_RenamePath(temporary_file, file_path));
    temporary_file_may_exist = false;
    SDL_Log("Stripped PNG metadata from %s", file_path);
    success = true;

error:
    if (temporary_file_may_exist) {
        nc__asset_baker_remove_temporary_file_preserving_error(temporary_file);
    }
    free(temporary_file);
    free(stripped_data);
    SDL_free(file_data);
    return success;
}

static bool nc__asset_baker_read_png_dimensions(const char* file_path, uint32_t* width, uint32_t* height) {
    static const uint8_t png_header_prefix[] = {
        0x89,  'P',  'N',  'G', '\r', '\n', 0x1A, '\n',
        0x00, 0x00, 0x00, 0x0D,  'I',  'H',  'D',  'R',
    };
    uint8_t header[24];
    bool success = false;
    SDL_IOStream* stream = SDL_IOFromFile(file_path, "rb");
    NC_CHECK_SDL_RESULT(stream);

    if (SDL_ReadIO(stream, header, sizeof(header)) != sizeof(header)) {
        NC_SET_ERROR("Could not read the PNG header from %s.", file_path);
        goto error;
    }
    if (memcmp(header, png_header_prefix, sizeof(png_header_prefix)) != 0) {
        NC_SET_ERROR("Texture %s does not have a valid PNG header.", file_path);
        goto error;
    }

    *width = nc__asset_baker_read_u32_be(header + 16);
    *height = nc__asset_baker_read_u32_be(header + 20);
    success = true;

error:
    if (stream) {
        SDL_CloseIO(stream);
    }
    return success;
}

static size_t nc__asset_baker_block_compressed_payload_size(const uint32_t width, const uint32_t height) {
    return ((size_t)width + 3) / 4 * (((size_t)height + 3) / 4) * 16;
}

static uint32_t nc__asset_baker_mip_level_count(uint32_t width, uint32_t height) {
    uint32_t largest_dimension = width > height ? width : height;
    uint32_t result = 1;
    while (largest_dimension > 1) {
        largest_dimension >>= 1;
        result++;
    }
    return result;
}

static size_t nc__asset_baker_block_compressed_mip_chain_size(
    uint32_t width,
    uint32_t height,
    const uint32_t mip_level_count
) {
    size_t result = 0;
    for (uint32_t mip_level = 0; mip_level < mip_level_count; mip_level++) {
        result += nc__asset_baker_block_compressed_payload_size(width, height);
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return result;
}

static bool nc__asset_baker_validate_astc_output(
    const nc__asset_baker_source_asset_info_t* asset_info,
    const void* output,
    const size_t output_size,
    const uint32_t source_width,
    const uint32_t source_height,
    const void** pixels,
    size_t* pixels_size
) {
    static const uint8_t expected_magic[] = { 0x13, 0xAB, 0xA1, 0x5C };
    if (output_size < sizeof(nc__asset_baker_astc_header_t)) {
        NC_SET_ERROR("Astcenc produced a truncated ASTC header for %s.", asset_info->file_path);
        return false;
    }

    const nc__asset_baker_astc_header_t* header = output;
    if (memcmp(header->magic, expected_magic, sizeof(expected_magic)) != 0) {
        NC_SET_ERROR("Astcenc produced an invalid ASTC magic value for %s.", asset_info->file_path);
        return false;
    }
    if (header->block_x != 4 || header->block_y != 4 || header->block_z != 1) {
        NC_SET_ERROR(
                "Astcenc produced %ux%ux%u blocks for %s; expected 4x4x1.",
                header->block_x,
                header->block_y,
                header->block_z,
                asset_info->file_path);
        return false;
    }

    const uint32_t width = nc__asset_baker_read_u24(header->dim_x);
    const uint32_t height = nc__asset_baker_read_u24(header->dim_y);
    const uint32_t depth = nc__asset_baker_read_u24(header->dim_z);
    if (width == 0 || height == 0 || depth != 1) {
        NC_SET_ERROR(
                "Astcenc produced invalid dimensions %ux%ux%u for %s.",
                width,
                height,
                depth,
                asset_info->file_path);
        return false;
    }
    if (width != source_width || height != source_height) {
        NC_SET_ERROR(
                "Astcenc changed the dimensions of %s from %ux%u to %ux%u.",
                asset_info->file_path,
                source_width,
                source_height,
                width,
                height);
        return false;
    }

    const size_t expected_pixels_size = nc__asset_baker_block_compressed_payload_size(width, height);
    if (output_size != sizeof(*header) + expected_pixels_size) {
        NC_SET_ERROR(
                "Astcenc produced an invalid payload size for %s: expected %zu bytes, got %zu.",
                asset_info->file_path,
                expected_pixels_size,
                output_size - sizeof(*header));
        return false;
    }

    *pixels = (const uint8_t*)output + sizeof(*header);
    *pixels_size = expected_pixels_size;
    return true;
}

static bool nc__asset_baker_build_temporary_texture_paths(
    const nc__asset_baker_source_asset_info_t* asset_info,
    char output_directory[FILENAME_MAX],
    char temporary_resized_file[FILENAME_MAX],
    char temporary_compressed_file[FILENAME_MAX]
) {
    const char* database_file_name = asset_info->output_database_file;
    for (const char* character = asset_info->output_database_file; *character; character++) {
        if (*character == '/' || *character == '\\') {
            database_file_name = character + 1;
        }
    }

    const size_t directory_length = (size_t)(database_file_name - asset_info->output_database_file);
    if (directory_length == 0) {
        memcpy(output_directory, "./", 3);
    } else {
        if (directory_length >= FILENAME_MAX) {
            NC_SET_ERROR("The texconv output directory is too long for %s.", asset_info->file_path);
            return false;
        }
        memcpy(output_directory, asset_info->output_database_file, directory_length);
        output_directory[directory_length] = '\0';
    }

    const int resized_length = snprintf(
            temporary_resized_file,
            FILENAME_MAX,
            "%s" NC__ASSET_BAKER_TEMPORARY_TEXTURE_PREFIX "%.*s.png",
            output_directory,
            (int)asset_info->asset_name.length,
            asset_info->asset_name.start);
    const int compressed_length = snprintf(
            temporary_compressed_file,
            FILENAME_MAX,
            "%s" NC__ASSET_BAKER_TEMPORARY_TEXTURE_PREFIX "%.*s.%s",
            output_directory,
            (int)asset_info->asset_name.length,
            asset_info->asset_name.start,
            asset_info->mobile ? "astc" : "dds");
    if (resized_length < 0 || resized_length >= FILENAME_MAX
            || compressed_length < 0 || compressed_length >= FILENAME_MAX) {
        NC_SET_ERROR("A temporary texture path is too long for %s.", asset_info->file_path);
        return false;
    }
    return true;
}

static bool nc__asset_baker_validate_dds_output(
    const nc__asset_baker_source_asset_info_t* asset_info,
    const void* output,
    const size_t output_size,
    const uint32_t source_width,
    const uint32_t source_height,
    const uint32_t expected_mip_level_count,
    const void** pixels,
    size_t* pixels_size
) {
    enum {
        NC__DDS_HEADER_SIZE = 124,
        NC__DDS_PIXEL_FORMAT_SIZE = 32,
        NC__DDS_DX10_HEADER_SIZE = 20,
        NC__DDS_FILE_HEADER_SIZE = 4 + NC__DDS_HEADER_SIZE + NC__DDS_DX10_HEADER_SIZE,
        NC__DDS_PIXEL_FORMAT_FLAG_FOURCC = 0x4,
        NC__DDS_DXGI_FORMAT_BC7_UNORM_SRGB = 99,
        NC__DDS_RESOURCE_DIMENSION_TEXTURE_2D = 3,
    };
    const uint8_t* bytes = output;
    if (output_size < NC__DDS_FILE_HEADER_SIZE) {
        NC_SET_ERROR("Texconv produced a truncated DDS header for %s.", asset_info->file_path);
        return false;
    }
    if (memcmp(bytes, "DDS ", 4) != 0) {
        NC_SET_ERROR("Texconv produced an invalid DDS magic value for %s.", asset_info->file_path);
        return false;
    }
    if (nc__asset_baker_read_u32_le(bytes + 4) != NC__DDS_HEADER_SIZE
            || nc__asset_baker_read_u32_le(bytes + 76) != NC__DDS_PIXEL_FORMAT_SIZE) {
        NC_SET_ERROR("Texconv produced invalid DDS header sizes for %s.", asset_info->file_path);
        return false;
    }

    const uint32_t pixel_format_flags = nc__asset_baker_read_u32_le(bytes + 80);
    if (!(pixel_format_flags & NC__DDS_PIXEL_FORMAT_FLAG_FOURCC) || memcmp(bytes + 84, "DX10", 4) != 0) {
        NC_SET_ERROR("Texconv did not produce a DX10 DDS header for %s.", asset_info->file_path);
        return false;
    }

    const uint32_t width = nc__asset_baker_read_u32_le(bytes + 16);
    const uint32_t height = nc__asset_baker_read_u32_le(bytes + 12);
    const uint32_t mip_count = nc__asset_baker_read_u32_le(bytes + 28);
    const uint32_t format = nc__asset_baker_read_u32_le(bytes + 128);
    const uint32_t resource_dimension = nc__asset_baker_read_u32_le(bytes + 132);
    const uint32_t misc_flags = nc__asset_baker_read_u32_le(bytes + 136);
    const uint32_t array_size = nc__asset_baker_read_u32_le(bytes + 140);
    const uint32_t caps2 = nc__asset_baker_read_u32_le(bytes + 112);
    if (format != NC__DDS_DXGI_FORMAT_BC7_UNORM_SRGB) {
        NC_SET_ERROR("Texconv produced DDS format %u for %s; expected BC7_UNORM_SRGB.", format, asset_info->file_path);
        return false;
    }
    if (resource_dimension != NC__DDS_RESOURCE_DIMENSION_TEXTURE_2D
            || misc_flags != 0
            || caps2 != 0
            || array_size != 1
            || mip_count != expected_mip_level_count) {
        NC_SET_ERROR(
                "Texconv did not produce one 2D image with %u mip level(s) for %s.",
                expected_mip_level_count,
                asset_info->file_path);
        return false;
    }
    if (width == 0 || height == 0 || width != source_width || height != source_height) {
        NC_SET_ERROR(
                "Texconv changed the dimensions of %s from %ux%u to %ux%u.",
                asset_info->file_path,
                source_width,
                source_height,
                width,
                height);
        return false;
    }

    const size_t expected_pixels_size = nc__asset_baker_block_compressed_mip_chain_size(
            width,
            height,
            expected_mip_level_count);
    if (output_size != NC__DDS_FILE_HEADER_SIZE + expected_pixels_size) {
        NC_SET_ERROR(
                "Texconv produced an invalid payload size for %s: expected %zu bytes, got %zu.",
                asset_info->file_path,
                expected_pixels_size,
                output_size - NC__DDS_FILE_HEADER_SIZE);
        return false;
    }

    *pixels = bytes + NC__DDS_FILE_HEADER_SIZE;
    *pixels_size = expected_pixels_size;
    return true;
}

static bool nc__asset_baker_bake_texture_asset(const nc__asset_baker_source_asset_info_t* asset_info) {
    bool success = false;
    void* compressor_output = NULL;
    void* mip_chain = NULL;
    void* compressed_pixels = NULL;
    char output_directory[FILENAME_MAX];
    char temporary_file[FILENAME_MAX];
    char temporary_resized_file[FILENAME_MAX];
    temporary_file[0] = '\0';
    temporary_resized_file[0] = '\0';

    if (asset_info->strip_png_metadata && !nc__asset_baker_strip_png_metadata(asset_info->file_path)) {
        goto error;
    }

    uint32_t width;
    uint32_t height;
    if (!nc__asset_baker_read_png_dimensions(asset_info->file_path, &width, &height)) {
        goto error;
    }
    if (width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX) {
        NC_SET_ERROR(
                "Texture %s is %ux%u, the valid dimensions are 1x1 through %ix%i.",
                asset_info->file_path,
                width,
                height,
                INT16_MAX,
                INT16_MAX);
        goto error;
    }
    if (!nc__asset_baker_build_temporary_texture_paths(
            asset_info,
            output_directory,
            temporary_resized_file,
            temporary_file)) {
        goto error;
    }

    const void* pixels = NULL;
    size_t pixels_size = 0;
    size_t compressor_output_size = 0;
    const uint32_t mip_level_count = asset_info->texture_type == NC_TEXTURE_TYPE_BLOCK
            ? nc__asset_baker_mip_level_count(width, height)
            : 1;
    if (asset_info->mobile) {
        // ASTC files contain one image, so concatenate their headerless payloads into the same mip-major layout used
        // by DDS. Runtime loading can then decompress and copy the complete chain as one allocation.
        pixels_size = nc__asset_baker_block_compressed_mip_chain_size(width, height, mip_level_count);
        mip_chain = malloc(pixels_size);
        size_t mip_chain_offset = 0;
        uint32_t mip_width = width;
        uint32_t mip_height = height;

        for (uint32_t mip_level = 0; mip_level < mip_level_count; mip_level++) {
            const char* mip_source_file = asset_info->file_path;
            if (mip_level > 0) {
                if (SDL_GetPathInfo(temporary_resized_file, NULL)) {
                    NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_resized_file));
                } else {
                    SDL_ClearError();
                }

                char mip_width_string[16];
                char mip_height_string[16];
                snprintf(mip_width_string, sizeof(mip_width_string), "%u", mip_width);
                snprintf(mip_height_string, sizeof(mip_height_string), "%u", mip_height);
                const char* texconv_parameters[] = {
                    asset_info->texconv_executable,
                    "-nologo",
                    "-y",
                    "-w", mip_width_string,
                    "-h", mip_height_string,
                    "-m", "1",
                    "-if", "BOX",
                    "-srgb",
                    "-f", "R8G8B8A8_UNORM_SRGB",
                    "-ft", "PNG",
                    "-o", output_directory,
                    "-px", NC__ASSET_BAKER_TEMPORARY_TEXTURE_PREFIX,
                    "--",
                    asset_info->file_path,
                    NULL,
                };
                if (!nc__asset_baker_execute_process(texconv_parameters)) {
                    goto error;
                }
                mip_source_file = temporary_resized_file;
            }

            if (SDL_GetPathInfo(temporary_file, NULL)) {
                NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_file));
            } else {
                SDL_ClearError();
            }

            const char* astcenc_parameters[] = {
                asset_info->astcenc_executable,
                "-cs",  // sRGB, for linear use -cl
                mip_source_file,
                temporary_file,
                "4x4",
                "-exhaustive",
                "-decode_unorm8",
                NULL,
            };
            if (!nc__asset_baker_execute_process(astcenc_parameters)) {
                goto error;
            }
            if (mip_level > 0) {
                NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_resized_file));
            }

            compressor_output = SDL_LoadFile(temporary_file, &compressor_output_size);
            NC_CHECK_SDL_RESULT(compressor_output);
            NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_file));

            const void* mip_pixels;
            size_t mip_pixels_size;
            if (!nc__asset_baker_validate_astc_output(
                    asset_info,
                    compressor_output,
                    compressor_output_size,
                    mip_width,
                    mip_height,
                    &mip_pixels,
                    &mip_pixels_size)) {
                goto error;
            }
            NC_ASSERT(mip_chain_offset + mip_pixels_size <= pixels_size);
            memcpy((uint8_t*)mip_chain + mip_chain_offset, mip_pixels, mip_pixels_size);
            mip_chain_offset += mip_pixels_size;
            SDL_free(compressor_output);
            compressor_output = NULL;

            mip_width = mip_width > 1 ? mip_width >> 1 : 1;
            mip_height = mip_height > 1 ? mip_height >> 1 : 1;
        }

        NC_ASSERT(mip_chain_offset == pixels_size);
        pixels = mip_chain;
    } else {
        if (SDL_GetPathInfo(temporary_file, NULL)) {
            NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_file));
        } else {
            SDL_ClearError();
        }
        const char* parameters[] = {
            asset_info->texconv_executable,
            "-nologo",
            "-y",
            "-m", asset_info->texture_type == NC_TEXTURE_TYPE_BLOCK ? "0" : "1",
            "-if", "BOX",
            "-srgbi",
            "-f", "BC7_UNORM_SRGB",
            "-bc", "x",
            "-dx10",
            "-ft", "DDS",
            "-o", output_directory,
            "-px", NC__ASSET_BAKER_TEMPORARY_TEXTURE_PREFIX,
            "--",
            asset_info->file_path,
            NULL,
        };
        if (!nc__asset_baker_execute_process(parameters)) {
            goto error;
        }
        compressor_output = SDL_LoadFile(temporary_file, &compressor_output_size);
        NC_CHECK_SDL_RESULT(compressor_output);
        NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_file));
        if (!nc__asset_baker_validate_dds_output(
                asset_info,
                compressor_output,
                compressor_output_size,
                width,
                height,
                mip_level_count,
                &pixels,
                &pixels_size)) {
            goto error;
        }
    }

    NC_ASSERT(pixels && pixels_size);

    size_t compressed_pixels_size;
    if (!nc__asset_baker_compress(
            asset_info->zstd_compression_context,
            pixels,
            pixels_size,
            &compressed_pixels,
            &compressed_pixels_size)) {
        goto error;
    }

    if (compressed_pixels_size > pixels_size) {
        SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "The compressed size of %s was larger than the original (%zu > %zu).",
                asset_info->file_path,
                compressed_pixels_size,
                pixels_size);
    }

    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_text(
            asset_info->database_context.insert_texture_asset,
            1,
            asset_info->mod_namespace.start,
            (int)asset_info->mod_namespace.length,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_text(
            asset_info->database_context.insert_texture_asset,
            2,
            asset_info->asset_name.start,
            (int)asset_info->asset_name.length,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_int(
            asset_info->database_context.insert_texture_asset,
            3,
            asset_info->texture_type));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_int(
            asset_info->database_context.insert_texture_asset,
            4,
            asset_info->mobile ? 1 : 2));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_int(
            asset_info->database_context.insert_texture_asset,
            5,
            width));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_int(
            asset_info->database_context.insert_texture_asset,
            6,
            height));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_blob(
            asset_info->database_context.insert_texture_asset,
            7,
            compressed_pixels,
            (int)compressed_pixels_size,
            SQLITE_STATIC));

    NC__CHECK_SQLITE_STEP(
            asset_info->database_context.output_database,
            sqlite3_step(asset_info->database_context.insert_texture_asset));

    NC__CHECK_SQLITE_RESULT(
            asset_info->database_context.output_database,
            sqlite3_reset(asset_info->database_context.insert_texture_asset));
    NC__CHECK_SQLITE_RESULT(
            asset_info->database_context.output_database,
            sqlite3_clear_bindings(asset_info->database_context.insert_texture_asset));

    SDL_Log("Baked texture asset %s", asset_info->file_path);
    success = true;

error:
    if (!success) {
        // SQLITE_STATIC bindings must be released before their backing allocations or borrowed slices expire.
        sqlite3_reset(asset_info->database_context.insert_texture_asset);
        sqlite3_clear_bindings(asset_info->database_context.insert_texture_asset);
    }
    if (temporary_resized_file[0]) {
        nc__asset_baker_remove_temporary_file_preserving_error(temporary_resized_file);
    }
    if (temporary_file[0]) {
        nc__asset_baker_remove_temporary_file_preserving_error(temporary_file);
    }
    SDL_free(compressor_output);
    free(mip_chain);
    free(compressed_pixels);
    return success;
}

static bool nc__asset_baker_bake_asset(const nc__asset_baker_source_asset_info_t* asset_info) {
    NC_ASSERT(asset_info->asset_type < NC_ASSET_BAKER_ASSET_TYPE_COUNT);

    switch (asset_info->asset_type) {
        case NC_ASSET_BAKER_ASSET_TYPE_SHADER:
            return nc__asset_baker_bake_shader_asset(asset_info);
        case NC_ASSET_BAKER_ASSET_TYPE_TEXTURE:
            return nc__asset_baker_bake_texture_asset(asset_info);
        default:
            NC_ASSERT(false);
            NC_SET_ERROR("Unrecognized asset type for file %s", asset_info->file_path);
            return false;
    }
}

static bool nc__asset_baker_get_child_path_info(
    const char* parent_path,
    const char* entry_name,
    char full_path[FILENAME_MAX],
    SDL_PathInfo* path_info
) {
    const int written = snprintf(full_path, FILENAME_MAX, "%s%s", parent_path, entry_name);
    if (written < 0) {
        NC_SET_ERROR("Failed to get the full asset file name.");
        return false;
    }

    if (written >= FILENAME_MAX) {
        NC_SET_ERROR("The asset file name is too long.");
        return false;
    }

    NC_CHECK_SDL_RESULT(SDL_GetPathInfo(full_path, path_info));

    return true;

error:
    return false;
}

static SDL_EnumerationResult nc__asset_baker_enumerate_asset_entries_callback(
    void* user_data,
    const char* parent_path,
    const char* entry_name
) {
    nc__asset_baker_source_asset_info_t* asset_info = user_data;

    char full_path[FILENAME_MAX];
    SDL_PathInfo path_info;
    if (!nc__asset_baker_get_child_path_info(parent_path, entry_name, full_path, &path_info)) {
        return SDL_ENUM_FAILURE;
    }
    asset_info->file_path = full_path;

    bool subtype_was_set = false;
    bool asset_is_valid = false;
    nc_string_slice_t slice;
    switch (path_info.type) {
        case SDL_PATHTYPE_FILE:
            nc_scan_identifier(&entry_name, &asset_info->asset_name);
            if (asset_info->asset_name.length == 0 || *entry_name != '.') {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid asset name, skipping: %s", full_path);
                break;
            }

            // skip the dot
            entry_name++;

            switch (asset_info->asset_type) {
                case NC_ASSET_BAKER_ASSET_TYPE_SHADER:
                    nc_scan_identifier(&entry_name, &slice);
                    if (nc_string_slice_equals_string(&slice, "inc")) {
                        SDL_Log("Skipping include file: %s", asset_info->file_path);
                        break;
                    }

                    if (slice.length == 0 || *entry_name != '\0') {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid shader stage, skipping: %s", full_path);
                        break;
                    }

                    // It's fine to use strcmp here instead of string slice equality since
                    // we have made sure the string ends with NULL.
                    if (strcmp(slice.start, "vert") == 0) {
                        asset_info->shader_stage = NC_SHADER_STAGE_VERTEX;
                        subtype_was_set = true;
                        asset_is_valid = true;
                    } else if (strcmp(slice.start, "frag") == 0) {
                        asset_info->shader_stage = NC_SHADER_STAGE_FRAGMENT;
                        subtype_was_set = true;
                        asset_is_valid = true;
                    } else {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid shader stage, skipping: %s", full_path);
                    }
                    break;
                case NC_ASSET_BAKER_ASSET_TYPE_TEXTURE:
                    if (!asset_info->texture_type) {
                        SDL_LogWarn(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "The texture is unclassified, skipping: %s",
                                full_path);
                        break;
                    }

                    if (strcmp(entry_name, "png") != 0) {
                        SDL_LogWarn(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "Invalid texture file extension, skipping: %s",
                                full_path);
                        break;
                    }
                    asset_is_valid = true;
                    break;
                default:
                    break;
            }
            break;
        case SDL_PATHTYPE_DIRECTORY:
            if (asset_info->asset_type == NC_ASSET_BAKER_ASSET_TYPE_TEXTURE && !asset_info->texture_type) {
                if (strcmp(entry_name, "block") == 0) {
                    asset_info->texture_type = NC_TEXTURE_TYPE_BLOCK;
                    subtype_was_set = true;
                } else if (strcmp(entry_name, "gui") == 0) {
                    asset_info->texture_type = NC_TEXTURE_TYPE_GUI;
                    subtype_was_set = true;
                }
            }

            NC_CHECK_SDL_RESULT(SDL_EnumerateDirectory(
                    full_path,
                    nc__asset_baker_enumerate_asset_entries_callback,
                    asset_info));
            break;
        default:
            break;
    }

    if (path_info.type == SDL_PATHTYPE_FILE && asset_is_valid) {
        if (!nc__asset_baker_bake_asset(asset_info)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to bake %s: %s", asset_info->file_path, SDL_GetError());
            return SDL_ENUM_FAILURE;
        }
    }

    if (subtype_was_set) {
        if (asset_info->asset_type == NC_ASSET_BAKER_ASSET_TYPE_SHADER) {
            asset_info->shader_stage = 0;
        } else {
            asset_info->texture_type = 0;
        }
    }

    return SDL_ENUM_CONTINUE;

error:
    return SDL_ENUM_FAILURE;
}

static SDL_EnumerationResult nc__asset_baker_enumerate_asset_types_callback(
    void* user_data,
    const char* parent_path,
    const char* entry_name
) {
    char full_path[FILENAME_MAX];
    SDL_PathInfo path_info;
    if (!nc__asset_baker_get_child_path_info(parent_path, entry_name, full_path, &path_info)) {
        return SDL_ENUM_FAILURE;
    }

    if (path_info.type != SDL_PATHTYPE_DIRECTORY) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "The path is not a directory: %s", full_path);
        return SDL_ENUM_CONTINUE;
    }

    nc__asset_baker_source_asset_info_t* asset_info = user_data;
    asset_info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_INVALID;
    asset_info->shader_stage = 0;
    if (strcmp(entry_name, "shader") == 0) {
        asset_info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_SHADER;
    } else if (strcmp(entry_name, "texture") == 0) {
        asset_info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_TEXTURE;
    }

    if (asset_info->asset_type == NC_ASSET_BAKER_ASSET_TYPE_INVALID) {
        return SDL_ENUM_CONTINUE;
    }

    const bool sdl_result = SDL_EnumerateDirectory(
            full_path,
            nc__asset_baker_enumerate_asset_entries_callback,
            asset_info);
    NC_CHECK_SDL_RESULT(sdl_result);
    asset_info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_INVALID;
    asset_info->shader_stage = 0;

    return SDL_ENUM_CONTINUE;

error:
    asset_info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_INVALID;
    asset_info->shader_stage = 0;
    return SDL_ENUM_FAILURE;
}

static SDL_EnumerationResult nc__asset_baker_enumerate_namespaces_callback(
    void* user_data,
    const char* parent_path,
    const char* entry_name
) {
    nc__asset_baker_source_asset_info_t* asset_info = user_data;

    char full_path[FILENAME_MAX];
    SDL_PathInfo path_info;
    if (!nc__asset_baker_get_child_path_info(parent_path, entry_name, full_path, &path_info)) {
        return SDL_ENUM_FAILURE;
    }

    if (path_info.type != SDL_PATHTYPE_DIRECTORY) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "The path is not a directory: %s", full_path);
        return SDL_ENUM_CONTINUE;
    }

    nc_scan_identifier(&entry_name, &asset_info->mod_namespace);
    if (asset_info->mod_namespace.length == 0 || *entry_name != '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid mod namespace, skipping: %s", full_path);
        return SDL_ENUM_CONTINUE;
    }

    const bool sdl_result = SDL_EnumerateDirectory(
            full_path,
            nc__asset_baker_enumerate_asset_types_callback,
            asset_info);
    NC_CHECK_SDL_RESULT(sdl_result);

    return SDL_ENUM_CONTINUE;

error:
    return SDL_ENUM_FAILURE;
}

static void nc__asset_baker_skip_slashes(const char** c) {
    while (**c && **c == '/') {
        (*c)++;
    }
}

static bool nc__asset_baker_try_parse_asset_path(
    const char* file_path,
    nc__asset_baker_source_asset_info_t* info
) {
    const char* c = file_path;
    nc__asset_baker_skip_slashes(&c);
    if (*c == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing mod namespace in path, skipping: %s", file_path);
        return false;
    }

    nc_scan_identifier(&c, &info->mod_namespace);
    if (info->mod_namespace.length == 0 || (*c != '\0' && *c != '/')) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid mod namespace in path, skipping: %s", file_path);
        return false;
    }

    nc__asset_baker_skip_slashes(&c);
    if (*c == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing asset type in path, skipping: %s", file_path);
        return false;
    }

    nc_string_slice_t string_slice;
    nc_scan_identifier(&c, &string_slice);
    if (string_slice.length == 0 || (*c != '\0' && *c != '/')) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid asset type in path, skipping: %s", file_path);
        return false;
    }

    nc__asset_baker_skip_slashes(&c);
    if (*c == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing asset name in path, skipping: %s", file_path);
        return false;
    }

    if (nc_string_slice_equals_string(&string_slice, "shader")) {
        info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_SHADER;
    } else if (nc_string_slice_equals_string(&string_slice, "texture")) {
        info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_TEXTURE;

        if (*c == '\0') {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing texture subtype in path, skipping: %s", file_path);
            return false;
        }

        nc_scan_identifier(&c, &string_slice);
        if (string_slice.length == 0 || (*c != '\0' && *c != '/')) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid texture subtype in path, skipping: %s", file_path);
            return false;
        }

        if (nc_string_slice_equals_string(&string_slice, "block")) {
            info->texture_type = NC_TEXTURE_TYPE_BLOCK;
        } else if (nc_string_slice_equals_string(&string_slice, "gui")) {
            info->texture_type = NC_TEXTURE_TYPE_GUI;
        } else {
            SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Texture subtype not recognized in path, skipping: %s",
                    file_path);
            return false;
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Asset type not recognized in path, skipping: %s", file_path);
        return false;
    }

    nc__asset_baker_skip_slashes(&c);
    if (*c == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing asset name in path, skipping: %s", file_path);
        return false;
    }

    nc_scan_identifier(&c, &info->asset_name);
    if (info->asset_name.length == 0 || (*c == '.' ? c[1] == '\0' : *c != '\0')) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid asset name in path, skipping: %s", file_path);
        return false;
    }

    if (*c != '.' || c[1] == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing asset file extension, skipping: %s", file_path);
        return false;
    }

    c++;
    nc_string_slice_t extension;
    nc_scan_identifier(&c, &extension);
    if (*c != '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unexpected asset file extension, skipping: %s", file_path);
        return false;
    }

    if (info->asset_type == NC_ASSET_BAKER_ASSET_TYPE_SHADER) {
        if (nc_string_slice_equals_string(&extension, "vert")) {
            info->shader_stage = NC_SHADER_STAGE_VERTEX;
        } else if (nc_string_slice_equals_string(&extension, "frag")) {
            info->shader_stage = NC_SHADER_STAGE_FRAGMENT;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid shader stage, skipping: %s", file_path);
            return false;
        }
    } else if (!nc_string_slice_equals_string(&extension, "png")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid texture file extension, skipping: %s", file_path);
        return false;
    }

    return true;
}

static void nc__asset_baker_reset_source_asset_identity(nc__asset_baker_source_asset_info_t* info) {
    info->mod_namespace = (nc_string_slice_t){ 0 };
    info->asset_name = (nc_string_slice_t){ 0 };
    info->file_path = NULL;
    info->asset_type = NC_ASSET_BAKER_ASSET_TYPE_INVALID;
    info->shader_stage = 0;
}

static bool nc__asset_baker_delete_asset(const nc__asset_baker_source_asset_info_t* asset_info) {
    sqlite3_stmt* statement;
    int subtype;
    if (asset_info->asset_type == NC_ASSET_BAKER_ASSET_TYPE_SHADER) {
        statement = asset_info->database_context.delete_shader_asset;
        subtype = asset_info->shader_stage;
    } else {
        NC_ASSERT(asset_info->asset_type == NC_ASSET_BAKER_ASSET_TYPE_TEXTURE);
        statement = asset_info->database_context.delete_texture_asset;
        subtype = asset_info->texture_type;
    }

    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_text(
            statement,
            1,
            asset_info->mod_namespace.start,
            (int)asset_info->mod_namespace.length,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_bind_text(
            statement,
            2,
            asset_info->asset_name.start,
            (int)asset_info->asset_name.length,
            SQLITE_STATIC));
    NC__CHECK_SQLITE_RESULT(
            asset_info->database_context.output_database,
            sqlite3_bind_int(statement, 3, subtype));
    NC__CHECK_SQLITE_STEP(asset_info->database_context.output_database, sqlite3_step(statement));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_reset(statement));
    NC__CHECK_SQLITE_RESULT(asset_info->database_context.output_database, sqlite3_clear_bindings(statement));

    SDL_Log("Deleted baked asset for missing source %s", asset_info->file_path);
    return true;

error:
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    return false;
}

static bool nc__asset_baker_process_changed_asset(
    const char* source_assets_directory,
    char* relative_path,
    nc__asset_baker_source_asset_info_t* info
) {
    // W*ndows is an abomination of an OS.
    for (char* position = relative_path; *position; position++) {
        if (*position == '\\') {
            *position = '/';
        }
    }

    nc__asset_baker_reset_source_asset_identity(info);
    if (strstr(relative_path, "/shader/") && strstr(relative_path, ".inc.")) {
        SDL_Log("Skipping include file: %s", relative_path);
        return true;
    }
    if (!nc__asset_baker_try_parse_asset_path(relative_path, info)) {
        nc__asset_baker_reset_source_asset_identity(info);
        return true;
    }

    char full_path[FILENAME_MAX];
    const size_t source_directory_length = strlen(source_assets_directory);
    const bool needs_separator = source_directory_length > 0
            && source_assets_directory[source_directory_length - 1] != '/'
            && source_assets_directory[source_directory_length - 1] != '\\';
    const int full_path_length = snprintf(
            full_path,
            sizeof(full_path),
            "%s%s%s",
            source_assets_directory,
            needs_separator ? "/" : "",
            relative_path);
    if (full_path_length < 0 || full_path_length >= (int)sizeof(full_path)) {
        NC_SET_ERROR("The changed asset path is too long: %s", relative_path);
        nc__asset_baker_reset_source_asset_identity(info);
        return false;
    }
    info->file_path = full_path;

    SDL_PathInfo path_info;
    if (SDL_GetPathInfo(full_path, &path_info)) {
        if (path_info.type != SDL_PATHTYPE_FILE) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "The changed asset is not a file, skipping: %s", full_path);
            nc__asset_baker_reset_source_asset_identity(info);
            return true;
        }
        if (!nc__asset_baker_bake_asset(info)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to bake %s: %s", full_path, SDL_GetError());
            nc__asset_baker_reset_source_asset_identity(info);
            return false;
        }
    } else {
        SDL_ClearError();
        if (!nc__asset_baker_delete_asset(info)) {
            nc__asset_baker_reset_source_asset_identity(info);
            return false;
        }
    }

    nc__asset_baker_reset_source_asset_identity(info);
    return true;
}

bool nc_asset_baker_bake_assets(const nc_asset_baker_options_t* options) {
    if (!options) {
        NC_SET_ERROR("Asset baker options must not be NULL.");
        return false;
    }
    if (!options->source_assets_directory) {
        NC_SET_ERROR("The source assets directory must not be NULL.");
        return false;
    }
    if (options->platform != NC_ASSET_BAKER_PLATFORM_DESKTOP
            && options->platform != NC_ASSET_BAKER_PLATFORM_MOBILE) {
        NC_SET_ERROR("Invalid asset baker platform %d.", options->platform);
        return false;
    }
    if (options->assets_to_build_count < 0
            || (options->assets_to_build_count > 0 && !options->assets_to_build)) {
        NC_SET_ERROR("Invalid changed asset list.");
        return false;
    }
    for (int i = 0; i < options->assets_to_build_count; i++) {
        if (!options->assets_to_build[i]) {
            NC_SET_ERROR("Changed asset path #%d must not be NULL.", i);
            return false;
        }
    }

    bool success = false;
    bool transaction_active = false;
    const char* output_database_file = options->output_database_file ? options->output_database_file : "assets.db";
    const bool output_database_existed = SDL_GetPathInfo(output_database_file, NULL);
    if (!output_database_existed) {
        SDL_ClearError();
    }
    const bool rebuild_all = options->assets_to_build_count == 0 || !output_database_existed;

    nc__asset_baker_source_asset_info_t info = {
        .output_database_file = output_database_file,
        .texconv_executable = options->texconv_executable ? options->texconv_executable : "texconv",
        .astcenc_executable = options->astcenc_executable ? options->astcenc_executable : "astcenc-sse4.1",
        .debug = options->debug,
        .mobile = options->platform == NC_ASSET_BAKER_PLATFORM_MOBILE,
        .strip_png_metadata = options->strip_png_metadata,
        .zstd_compression_context = ZSTD_createCCtx(),
    };

    char temporary_database_file[FILENAME_MAX];
    const char* database_file = output_database_file;
    if (rebuild_all) {
        const int temporary_path_length = snprintf(
                temporary_database_file,
                sizeof(temporary_database_file),
                "%s.tmp",
                output_database_file);
        if (temporary_path_length < 0 || temporary_path_length >= (int)sizeof(temporary_database_file)) {
            NC_SET_ERROR("The temporary asset database path is too long: %s.tmp", output_database_file);
            goto error;
        }
        database_file = temporary_database_file;

        if (SDL_GetPathInfo(temporary_database_file, NULL)) {
            NC_CHECK_SDL_RESULT(SDL_RemovePath(temporary_database_file));
        } else {
            SDL_ClearError();
        }
    }

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_open_v2(
            database_file,
            &info.database_context.output_database,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            NULL));

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_exec(
            info.database_context.output_database,
            "BEGIN IMMEDIATE",
            NULL,
            NULL,
            NULL));
    transaction_active = true;

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_exec(
            info.database_context.output_database,
            "PRAGMA user_version = 1;\n"
            "CREATE TABLE IF NOT EXISTS ShaderAsset ("
                "namespace TEXT NOT NULL, "
                "name TEXT NOT NULL, "
                "subtype INT NOT NULL, "
                "spirv_bytecode BLOB NOT NULL, "
                "UNIQUE(namespace, name, subtype)"
            ") STRICT;\n"
            "CREATE TABLE IF NOT EXISTS TextureAsset ("
                "namespace TEXT NOT NULL, "
                "name TEXT NOT NULL, "
                "subtype INT NOT NULL, "
                "format INT NOT NULL, "
                "width INT NOT NULL, "
                "height INT NOT NULL, "
                "pixels BLOB NOT NULL, "
                "UNIQUE(namespace, name, subtype, format)"
            ") STRICT;\n",
            NULL,
            NULL,
            NULL));

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_prepare_v2(
            info.database_context.output_database,
            "REPLACE INTO ShaderAsset (namespace, name, subtype, spirv_bytecode) VALUES (?, ?, ?, ?)",
            -1,
            &info.database_context.insert_shader_asset,
            NULL));

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_prepare_v2(
            info.database_context.output_database,
            "REPLACE INTO TextureAsset "
            "(namespace, name, subtype, format, width, height, pixels) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            -1,
            &info.database_context.insert_texture_asset,
            NULL));

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_prepare_v2(
            info.database_context.output_database,
            "DELETE FROM ShaderAsset WHERE namespace = ? AND name = ? AND subtype = ?",
            -1,
            &info.database_context.delete_shader_asset,
            NULL));

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_prepare_v2(
            info.database_context.output_database,
            "DELETE FROM TextureAsset WHERE namespace = ? AND name = ? AND subtype = ?",
            -1,
            &info.database_context.delete_texture_asset,
            NULL));

    SDL_Log("Source assets directory: %s", options->source_assets_directory);
    if (rebuild_all) {
        SDL_Log("Asset build requested for all source assets.");
        const bool sdl_result = SDL_EnumerateDirectory(
                options->source_assets_directory,
                nc__asset_baker_enumerate_namespaces_callback,
                &info);
        NC_CHECK_SDL_RESULT(sdl_result);
    } else {
        SDL_Log("Asset build requested for %d changed source asset(s).", options->assets_to_build_count);
        for (int i = 0; i < options->assets_to_build_count; i++) {
            if (!nc__asset_baker_process_changed_asset(
                    options->source_assets_directory,
                    options->assets_to_build[i],
                    &info)) {
                goto error;
            }
        }
    }

    NC__CHECK_SQLITE_RESULT(info.database_context.output_database, sqlite3_exec(
            info.database_context.output_database,
            "COMMIT",
            NULL,
            NULL,
            NULL));
    transaction_active = false;
    success = true;

error:
    if (transaction_active) {
        const int rollback_result = sqlite3_exec(info.database_context.output_database, "ROLLBACK", NULL, NULL, NULL);
        if (rollback_result != SQLITE_OK) {
            SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to roll back the asset database: %s",
                    sqlite3_errmsg(info.database_context.output_database));
        }
    }

    sqlite3_finalize(info.database_context.delete_texture_asset);
    sqlite3_finalize(info.database_context.delete_shader_asset);
    sqlite3_finalize(info.database_context.insert_texture_asset);
    sqlite3_finalize(info.database_context.insert_shader_asset);
    if (info.database_context.output_database) {
        const int sqlite3_result = sqlite3_close(info.database_context.output_database);
        if (sqlite3_result != SQLITE_OK && success) {
            NC_SET_ERROR("Failed to close the asset database: %s", sqlite3_errstr(sqlite3_result));
            success = false;
        }
        info.database_context.output_database = NULL;
    }
    ZSTD_freeCCtx(info.zstd_compression_context);

    if (success && rebuild_all && !SDL_RenamePath(temporary_database_file, output_database_file)) {
        NC_SET_ERROR("Failed to replace %s with the newly baked database: %s", output_database_file, SDL_GetError());
        success = false;
    }
    if (!success && rebuild_all && SDL_GetPathInfo(temporary_database_file, NULL)) {
        if (!SDL_RemovePath(temporary_database_file)) {
            SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to remove the incomplete asset database %s: %s",
                    temporary_database_file,
                    SDL_GetError());
        }
    }

    return success;
}
