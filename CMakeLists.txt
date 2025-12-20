cmake_minimum_required(VERSION 3.17)

project(novacube C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NC_SOURCES
        libs/cvkm/cvkm.h
	    src/main.c)

if(ANDROID)
    add_library(novacube SHARED ${NC_SOURCES})
else()
    add_executable(novacube ${NC_SOURCES})
endif()

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
target_link_libraries(novacube PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(novacube PRIVATE /W4 /WX)
else()
    target_compile_options(novacube PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_include_directories(novacube PRIVATE include libs/cvkm)
