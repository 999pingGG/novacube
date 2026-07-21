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

set(NC_SOURCES
        libs/cvkm/cvkm.h
        libs/nuklear/nuklear.h
        libs/rapidhash/rapidhash.h
        src/block.c
        src/block_meshing.c
        src/camera.c
        src/chunk.c
        src/chunk_meshing.c
        src/cvar.c
        src/error_handling.c
        src/gui.c
        src/main.c
        src/mesher.c
        src/renderer.c
        src/string_builder.c
        src/terrain.c
        src/terrain_generation.c
        src/vma_implementation.cpp)

if(ANDROID)
    add_library(novacube SHARED ${NC_SOURCES})
else()
    add_executable(novacube ${NC_SOURCES})
endif()

find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(novacube PRIVATE ${MATH_LIBRARY})
endif()

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
target_link_libraries(novacube PRIVATE SDL3::SDL3)

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
    target_compile_definitions(novacube PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(novacube PRIVATE /W4 /WX /Zc:preprocessor)
    if(TARGET_X86)
        target_compile_options(novacube PRIVATE /arch:AVX2)
    elseif(TARGET_ARM)
        target_compile_options(novacube PRIVATE /arch:VFPv4)
    endif()
else()
    set_source_files_properties(src/vma_implementation.cpp PROPERTIES COMPILE_OPTIONS -w)
    target_compile_options(novacube PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-gnu-zero-variadic-macro-arguments
            $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
            $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
            $<$<COMPILE_LANGUAGE:CXX>:-nostdlib++>)
    if(TARGET_X86 AND NOT ANDROID)
        target_compile_options(novacube PRIVATE -march=haswell)
    endif()
    target_link_options(novacube PRIVATE -nostdlib++)
endif()

target_include_directories(novacube PRIVATE
        include
        libs/cvkm
        libs/nuklear
        libs/rapidhash
        libs/tds/include
        libs/vma
        libs/volk)

if(NOT ANDROID AND (WIN32 OR UNIX))
    add_custom_command(
            TARGET novacube
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:novacube>/assets
            COMMENT "Creating assets symlink next to executable.")
endif()

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

set_target_properties(novacube PROPERTIES
        DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT novacube)
