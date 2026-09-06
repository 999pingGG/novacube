cmake_minimum_required(VERSION 3.17)

project(novacube LANGUAGES C CXX VERSION 0.11.0.0 HOMEPAGE_URL "https://novacubegame.net")

set(NC_COMPANY_NAME CoupledSystems)
set(NC_PRODUCT_NAME Novacube)
set(NC_PACKAGE_NAME me.elinge.novacube)
string(REPLACE "." "," NC_VULKAN_APPLICATION_VERSION ${PROJECT_VERSION})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NC_BUILD_ASSET_BAKER_ONLY "Build only the host-runnable asset baker." OFF)

set(NC_SQLITE_COMPILE_DEFINITIONS
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_DQS=0
        SQLITE_LIKE_DOESNT_MATCH_BLOBS
        SQLITE_THREADSAFE=0
        SQLITE_OMIT_AUTHORIZATION
        SQLITE_OMIT_COMPLETE
        SQLITE_OMIT_DECLTYPE
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_DESERIALIZE
        SQLITE_OMIT_GENERATED_COLUMNS
        SQLITE_OMIT_GET_TABLE
        SQLITE_OMIT_JSON
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_OMIT_LOCALTIME
        SQLITE_OMIT_PROGRESS_CALLBACK
        SQLITE_OMIT_SHARED_CACHE
        SQLITE_OMIT_TRACE
        SQLITE_OMIT_UTF16)

set(NC_COMMON_SOURCES
        include/novacube/error_handling.h
        include/novacube/standard_functions.h
        include/novacube/string_handling.h
        src/error_handling.c
        src/string_handling.c
        vendor/sqlite3/sqlite3.c
        vendor/sqlite3/sqlite3.h
        vendor/zstd/zstd.c
        vendor/zstd/zstd.h
        vendor/zstd/zstd_errors.h)
set(NC_COMMON_INCLUDE_DIRECTORIES
        include
        vendor/sqlite3
        vendor/zstd)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_library(MATH_LIBRARY m)

set(NC_IGNORED_WARNINGS_FILES
        src/vma_implementation.cpp
        vendor/sqlite3/sqlite3.c
        vendor/zstd/zstd.c)

if(MSVC)
    set(NC_COMMON_COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
    set(NC_COMMON_COMPILE_OPTIONS /W4 /WX /Zc:preprocessor)
    set_source_files_properties(${NC_IGNORED_WARNINGS_FILES} PROPERTIES COMPILE_FLAGS /W0)
else()
    set(NC_COMMON_COMPILE_OPTIONS
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-gnu-zero-variadic-macro-arguments)
    set_source_files_properties(${NC_IGNORED_WARNINGS_FILES} PROPERTIES COMPILE_FLAGS -w)
endif()

function(nc_configure_common_target target)
    target_compile_definitions(${target} PRIVATE
            ${NC_SQLITE_COMPILE_DEFINITIONS}
            ${NC_COMMON_COMPILE_DEFINITIONS})
    target_compile_options(${target} PRIVATE
            ${NC_COMMON_COMPILE_OPTIONS})
    target_include_directories(${target} PRIVATE
            ${NC_COMMON_INCLUDE_DIRECTORIES})
    target_link_libraries(${target} PRIVATE SDL3::SDL3)
    if(MATH_LIBRARY)
        target_link_libraries(${target} PRIVATE ${MATH_LIBRARY})
    endif()
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL3::SDL3>
                        $<TARGET_FILE_DIR:${target}>
                COMMENT "Copying the SDL3 runtime for ${target}."
                VERBATIM)
    endif()
endfunction()

if(NC_BUILD_ASSET_BAKER_ONLY)
    if(ANDROID)
        message(FATAL_ERROR "The standalone asset baker must be compiled with a host toolchain.")
    endif()

    add_executable(novacube_asset_baker
            include/novacube/argument_parser.h
            include/novacube/asset_baker.h
            include/novacube/asset_types.h
            src/argument_parser.c
            src/asset_baker.c
            src/asset_baker_main.c
            ${NC_COMMON_SOURCES})
    nc_configure_common_target(novacube_asset_baker)

    # Gradle always configures and builds this target as Release.
    set_target_properties(novacube_asset_baker PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
    return()
endif()

set(NC_SOURCES
        include/novacube/asset_manager.h
        include/novacube/asset_types.h
        include/novacube/block.h
        include/novacube/build_info.h
        include/novacube/camera.h
        include/novacube/chunk.h
        include/novacube/cvar.h
        include/novacube/cvkm.h
        include/novacube/entity_command.h
        include/novacube/gui.h
        include/novacube/gui_view.h
        include/novacube/intrinsics.h
        include/novacube/macros.h
        include/novacube/mesher.h
        include/novacube/player_input.h
        include/novacube/renderer.h
        include/novacube/terrain.h
        include/novacube/terrain_generation.h
        src/asset_manager.c
        src/block.c
        src/block_meshing.c
        src/camera.c
        src/chunk.c
        src/chunk_meshing.c
        src/cvar.c
        src/fast_noise_lite_implementation.c
        src/gui.c
        src/main.c
        src/mesher.c
        src/player_input.c
        src/renderer.c
        src/terrain.c
        src/terrain_generation.c
        src/vma_implementation.cpp
        vendor/clay/clay.h
        vendor/cvkm/cvkm.h
        vendor/FastNoiseLite/FastNoiseLite.h
        vendor/rapidhash/rapidhash.h
        vendor/vma/vk_mem_alloc.h
        vendor/volk/volk.h
        ${NC_COMMON_SOURCES})
if(ANDROID)
    list(APPEND NC_SOURCES
            include/novacube/android_asset_vfs.h
            src/android_asset_vfs.c)
else()
    list(APPEND NC_SOURCES
            include/novacube/argument_parser.h
            include/novacube/asset_baker.h
            src/argument_parser.c
            src/asset_baker.c)
endif()

if(ANDROID)
    add_library(novacube SHARED ${NC_SOURCES})
else()
    add_executable(novacube ${NC_SOURCES})
endif()

nc_configure_common_target(novacube)
if(ANDROID)
    find_library(ANDROID_LIBRARY android)
    find_library(LOG_LIBRARY log)
    if((NOT ANDROID_LIBRARY) OR (NOT LOG_LIBRARY))
        message(FATAL_ERROR "The Android NDK libraries were not found.")
    endif()
    target_link_libraries(novacube PRIVATE ${ANDROID_LIBRARY} ${LOG_LIBRARY})
endif()

find_package(Vulkan REQUIRED COMPONENTS glslc)
target_include_directories(novacube PRIVATE ${Vulkan_INCLUDE_DIRS})

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH_LOWER)

if(ARCH_LOWER MATCHES "arm" OR ARCH_LOWER MATCHES "aarch64")
    message(STATUS "Target Architecture: ARM")
    set(TARGET_ARM TRUE)
elseif(ARCH_LOWER MATCHES "x86" OR ARCH_LOWER MATCHES "amd64" OR ARCH_LOWER MATCHES "i386" OR ARCH_LOWER MATCHES "i686")
    message(STATUS "Target Architecture: x86/x64")
    set(TARGET_X86 TRUE)
else()
    message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(MSVC)
    if(TARGET_X86)
        target_compile_options(novacube PRIVATE /arch:AVX2)
    elseif(TARGET_ARM)
        target_compile_options(novacube PRIVATE /arch:VFPv4)
    endif()
else()
    target_compile_options(novacube PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
            $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
            $<$<COMPILE_LANGUAGE:CXX>:-nostdlib++>)
    if(TARGET_X86 AND NOT ANDROID)
        target_compile_options(novacube PRIVATE -march=haswell)
    endif()
    target_link_options(novacube PRIVATE -nostdlib++)
endif()

target_include_directories(novacube PRIVATE
        vendor/clay
        vendor/cvkm
        vendor/FastNoiseLite
        vendor/rapidhash
        vendor/tds/include
        vendor/vma
        vendor/volk)

set(NC_BUILD_INFO_TEMPLATE "${CMAKE_SOURCE_DIR}/include/novacube/build_info.h.in")
set(NC_BUILD_INFO_HEADER "${CMAKE_SOURCE_DIR}/include/novacube/build_info.h")

add_custom_target(novacube_build_info ALL
        COMMAND ${CMAKE_COMMAND}
                "-DNC_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DNC_BUILD_INFO_TEMPLATE=${NC_BUILD_INFO_TEMPLATE}"
                "-DNC_BUILD_INFO_HEADER=${NC_BUILD_INFO_HEADER}"
                "-DPROJECT_VERSION=${PROJECT_VERSION}"
                "-DNC_COMPANY_NAME=${NC_COMPANY_NAME}"
                "-DNC_PRODUCT_NAME=${NC_PRODUCT_NAME}"
                "-DNC_PACKAGE_NAME=${NC_PACKAGE_NAME}"
                "-DNC_VULKAN_APPLICATION_VERSION=${NC_VULKAN_APPLICATION_VERSION}"
                -P "${CMAKE_SOURCE_DIR}/cmake/generate_build_info.cmake"
        BYPRODUCTS ${NC_BUILD_INFO_HEADER}
        COMMENT "Generating build info header."
        VERBATIM)
add_dependencies(novacube novacube_build_info)

if(NOT ANDROID)
    # Keep asset processing in the default desktop build without making it part of the executable target itself. The
    # asset compiler is the game executable, so this target must wait until novacube exists before checking assets.
    add_custom_target(novacube_assets ALL
            COMMAND ${CMAKE_COMMAND}
                    "-DNC_ASSET_STATE_FILE=${CMAKE_CURRENT_BINARY_DIR}/novacube-assets-$<CONFIG>.state"
                    "-DNC_ASSET_COMPILER=$<TARGET_FILE:novacube>"
                    "-DNC_OUTPUT_ASSET_DATABASE=$<TARGET_FILE_DIR:novacube>/assets.db"
                    "-DNC_DEBUG=$<CONFIG:Debug>"
                    -P "${CMAKE_SOURCE_DIR}/cmake/check_assets.cmake"
            DEPENDS novacube
            COMMENT "Checking source assets for changes."
            VERBATIM)

    # The asset baker uses the game executable, so novacube_assets must depend on novacube. A separate startup
    # target preserves that ordering while ensuring IDE build/run commands always perform the asset check.
    add_custom_target(novacube_dev
            DEPENDS novacube_assets)
    set_target_properties(novacube_dev PROPERTIES
            VS_DEBUGGER_COMMAND "$<TARGET_FILE:novacube>"
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>")
endif()

set_target_properties(novacube PROPERTIES
        DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>")
if(NOT ANDROID)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT novacube_dev)
else()
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT novacube)
endif()
