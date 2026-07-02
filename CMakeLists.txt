cmake_minimum_required(VERSION 3.17)

project(novacube LANGUAGES C VERSION 0.8.0.1 HOMEPAGE_URL "https://novacubegame.net")

set(NC_COMPANY_NAME CoupledSystems)
set(NC_PRODUCT_NAME Novacube)
set(NC_PACKAGE_NAME me.elinge.novacube)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NC_SOURCES
        libs/cvkm/cvkm.h
        libs/nuklear/nuklear.h
        libs/rapidhash/rapidhash.h
        src/camera.c
        src/cvar.c
        src/error_handling.c
        src/gui.c
        src/renderer.c
        src/string_builder.c
        src/terrain.c
        src/main.c)

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

if(MSVC)
    target_compile_options(novacube PRIVATE /W4 /WX /Zc:preprocessor)
else()
    target_compile_options(novacube PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-gnu-zero-variadic-macro-arguments)
endif()

target_include_directories(novacube PRIVATE include libs/cvkm libs/nuklear libs/tds/include libs/rapidhash)

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
                -P "${CMAKE_SOURCE_DIR}/cmake/generate_build_info.cmake"
        BYPRODUCTS ${NC_BUILD_INFO_HEADER}
        COMMENT "Generating build info header."
        VERBATIM)
add_dependencies(novacube novacube_build_info)

set_target_properties(novacube PROPERTIES
        DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT novacube)
