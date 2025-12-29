cmake_minimum_required(VERSION 3.17)

project(novacube LANGUAGES C VERSION 0.1.0.0 HOMEPAGE_URL "https://novacubegame.net")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NC_SOURCES
        libs/cvkm/cvkm.h
        libs/rapidhash/rapidhash.h
        src/main.c)

if(ANDROID)
    add_library(novacube SHARED ${NC_SOURCES})
else()
    add_executable(novacube ${NC_SOURCES})
endif()

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
target_link_libraries(novacube PRIVATE SDL3::SDL3)

find_package(Vulkan REQUIRED COMPONENTS glslc)
target_include_directories(novacube PRIVATE ${Vulkan_INCLUDE_DIRS})

if(MSVC)
    target_compile_options(novacube PRIVATE /W4 /WX)
else()
    target_compile_options(novacube PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_include_directories(novacube PRIVATE include libs/cvkm libs/tds/include libs/rapidhash)

if (NOT ANDROID AND (WIN32 OR UNIX))
    add_custom_command(
            TARGET novacube
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:novacube>/assets
            COMMENT "Creating assets symlink next to executable.")
endif()

execute_process(
        COMMAND git describe --tags --dirty --always
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(
        COMMAND git rev-parse HEAD
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE)

target_compile_definitions(novacube PRIVATE
        NC__VERSION="v${PROJECT_VERSION}"
        NC__GIT_HASH="${GIT_HASH}"
        NC__GIT_DESCRIBE="${GIT_DESCRIBE}")

set_target_properties(novacube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:novacube>")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT novacube)
