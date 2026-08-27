# This script runs from the desktop asset target or an Android Gradle bake task after a compatible host asset compiler
# is available. It keeps a compact snapshot of src-assets and invokes that compiler only when the snapshot changes.
# CMake variables keep the scanner independent of the caller and generator. Repository paths are derived from this
# script's location so callers only provide values that actually vary between build contexts.

get_filename_component(NC_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(NC_SOURCE_ASSET_DIR "${NC_SOURCE_DIR}/src-assets")

foreach(required_variable IN ITEMS
        NC_ASSET_STATE_FILE
        NC_ASSET_COMPILER
        NC_OUTPUT_ASSET_DATABASE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} must be defined.")
    endif()
endforeach()

if(NOT DEFINED NC_ASSET_PLATFORM)
    set(NC_ASSET_PLATFORM desktop)
endif()
if(NOT NC_ASSET_PLATFORM STREQUAL "desktop" AND NOT NC_ASSET_PLATFORM STREQUAL "mobile")
    message(FATAL_ERROR "NC_ASSET_PLATFORM must be desktop or mobile.")
endif()
if(NC_DEBUG)
    set(asset_debug_signature 1)
else()
    set(asset_debug_signature 0)
endif()
set(asset_build_signature "platform=${NC_ASSET_PLATFORM};debug=${asset_debug_signature}")

# Discovering the files at build time, rather than CMake configuration time, makes additions and deletions visible
# without regenerating the CMake project. The scan is repeated after baking because PNG metadata stripping can rewrite
# source files; the persisted state must describe those final bytes.
macro(nc_collect_source_asset_state)
    file(GLOB_RECURSE source_assets
            LIST_DIRECTORIES FALSE
            RELATIVE "${NC_SOURCE_ASSET_DIR}"
            "${NC_SOURCE_ASSET_DIR}/*")
    list(SORT source_assets)

    # Each state line is: <whole-second modification time> <size in bytes> <relative path>. This avoids reading asset
    # contents on every build for hashing and comparison. The size also catches most changes that happen too quickly to
    # produce a new timestamp.
    set(current_state "signature ${asset_build_signature}\n")
    foreach(asset IN LISTS source_assets)
        file(TIMESTAMP "${NC_SOURCE_ASSET_DIR}/${asset}" asset_timestamp "%s" UTC)
        file(SIZE "${NC_SOURCE_ASSET_DIR}/${asset}" asset_size)
        set(asset_fingerprint "${asset_timestamp} ${asset_size}")
        string(APPEND current_state "${asset_fingerprint} ${asset}\n")

        # This hashes only the relative path string, not the file contents. CMake variable names cannot safely contain
        # an arbitrary asset path, so the hash provides a fixed-size key for the lookup tables below.
        string(SHA256 asset_key "${asset}")
        set("current_fingerprint_${asset_key}" "${asset_fingerprint}")
    endforeach()
endmacro()

nc_collect_source_asset_state()

set(rebuild_all FALSE)
set(changed_assets "")

# No previous state means this is the first asset build for the current build configuration. Passing the source root
# without any changed paths signals that the asset compiler should rebuild everything.
if(NOT EXISTS "${NC_ASSET_STATE_FILE}" OR NOT EXISTS "${NC_OUTPUT_ASSET_DATABASE}")
    set(rebuild_all TRUE)
else()
    file(READ "${NC_ASSET_STATE_FILE}" previous_state)

    # The common case ends here after one metadata scan and a string comparison; the asset compiler is not started.
    if(previous_state STREQUAL current_state)
        return()
    endif()

    # Parse the previous snapshot into a lookup table. A malformed or obsolete state format cannot be compared safely,
    # so fall back to a full rebuild instead of risking stale output.
    file(STRINGS "${NC_ASSET_STATE_FILE}" previous_state_lines)
    if(NOT previous_state_lines)
        set(rebuild_all TRUE)
    else()
        list(POP_FRONT previous_state_lines previous_signature)
        if(NOT previous_signature STREQUAL "signature ${asset_build_signature}")
            set(rebuild_all TRUE)
        endif()
    endif()
    foreach(line IN LISTS previous_state_lines)
        if(rebuild_all)
            break()
        elseif(NOT line MATCHES "^([0-9]+) ([0-9]+) (.*)$")
            set(rebuild_all TRUE)
            break()
        endif()

        set(asset_fingerprint "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
        set(asset "${CMAKE_MATCH_3}")
        string(SHA256 asset_key "${asset}")
        set("previous_fingerprint_${asset_key}" "${asset_fingerprint}")
        list(APPEND previous_assets "${asset}")
    endforeach()

    if(NOT rebuild_all)
        # New paths have no previous fingerprint; modified paths have a different timestamp or size.
        foreach(asset IN LISTS source_assets)
            string(SHA256 asset_key "${asset}")
            if(NOT DEFINED previous_fingerprint_${asset_key} OR
                    NOT "${previous_fingerprint_${asset_key}}" STREQUAL "${current_fingerprint_${asset_key}}")
                list(APPEND changed_assets "${asset}")
            endif()
        endforeach()

        # Paths present only in the previous snapshot were deleted. They are still passed to the compiler so it can
        # remove or otherwise invalidate their generated output even though the source file no longer exists.
        foreach(asset IN LISTS previous_assets)
            string(SHA256 asset_key "${asset}")
            if(NOT DEFINED current_fingerprint_${asset_key})
                list(APPEND changed_assets "${asset}")
            endif()
        endforeach()

        # Shader compilers expand includes outside CMake's dependency graph. A changed or deleted shader include can
        # therefore affect any shader entry point; rebuild all of them to keep the baked database up to date.
        # An optimization for the future is parsing the shaders to see which ones actually include changed files
        # to recompile them, too.
        set(shader_include_changed FALSE)
        foreach(asset IN LISTS changed_assets)
            if(asset MATCHES "/shader/.*\\.inc\\.")
                set(shader_include_changed TRUE)
                break()
            endif()
        endforeach()

        if(shader_include_changed)
            foreach(asset IN LISTS source_assets)
                if(asset MATCHES "/shader/.*\\.(vert|frag)$" AND NOT asset MATCHES "\\.inc\\.")
                    list(APPEND changed_assets "${asset}")
                endif()
            endforeach()
        endif()

        list(REMOVE_DUPLICATES changed_assets)
        list(SORT changed_assets)
    endif()
endif()

# Every invocation supplies the absolute source-assets root. Incremental rebuilds then append each changed path
# relative to that root as its own command-line argument.
if(rebuild_all)
    message(STATUS "Asset database or source asset state is missing; rebuilding all assets.")
    set(asset_arguments "")
else()
    list(LENGTH changed_assets changed_asset_count)
    message(STATUS "Rebuilding ${changed_asset_count} changed source asset(s).")
    set(asset_arguments ${changed_assets})
endif()

# Run from the repository root so the asset compiler implementation can resolve src-assets and assets consistently.
set(COMPILER_COMMAND
        "${NC_ASSET_COMPILER}"
        --build-assets "${NC_SOURCE_ASSET_DIR}" ${asset_arguments}
        -o "${NC_OUTPUT_ASSET_DATABASE}"
        --platform "${NC_ASSET_PLATFORM}"
        --strip-png-metadata)
if(NC_DEBUG)
    list(APPEND COMPILER_COMMAND --debug)
endif()
execute_process(
        COMMAND ${COMPILER_COMMAND}
        WORKING_DIRECTORY "${NC_SOURCE_DIR}"
        RESULT_VARIABLE asset_compiler_result)
if(NOT asset_compiler_result EQUAL 0)
    string(JOIN " " compiler_command_display ${COMPILER_COMMAND})
    message(FATAL_ERROR
            "Asset compiler command ${compiler_command_display} failed with exit code ${asset_compiler_result}.")
endif()

# Metadata stripping may have changed source timestamps and sizes. Persist the post-bake state to avoid rebuilding the
# same PNGs again on the next otherwise unchanged build.
nc_collect_source_asset_state()

# Only advance the state after a successful build so failures are retried next time. Writing a temporary file and then
# renaming it prevents an interrupted write from leaving behind a partially valid snapshot.
get_filename_component(asset_state_directory "${NC_ASSET_STATE_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${asset_state_directory}")
set(asset_state_temporary_file "${NC_ASSET_STATE_FILE}.tmp")
file(WRITE "${asset_state_temporary_file}" "${current_state}")
file(RENAME "${asset_state_temporary_file}" "${NC_ASSET_STATE_FILE}")
