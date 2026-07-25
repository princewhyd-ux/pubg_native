cmake_minimum_required(VERSION 3.22.1)

project(pubg_native)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(
    -DPLATFORM_ANDROID
    -DGRAPHICS_API_OPENGL_ES2
)

set(RAYLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/raylib_src)

include_directories(
    ${RAYLIB_DIR}
    ${RAYLIB_DIR}/external/glfw/include
)

add_library(pubg_native SHARED

    main.cpp
    settings.cpp
    animations.cpp
    collision.cpp
    driving.cpp
    combat.cpp
    npc.cpp

    ${RAYLIB_DIR}/rcore.c
    ${RAYLIB_DIR}/rmodels.c
    ${RAYLIB_DIR}/rtextures.c
    ${RAYLIB_DIR}/rtext.c
    ${RAYLIB_DIR}/rshapes.c
    ${RAYLIB_DIR}/raudio.c
    ${RAYLIB_DIR}/utils.c
)

find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv2-lib GLESv2)
find_library(OpenSLES-lib OpenSLES)
find_library(m-lib m)
find_library(dl-lib dl)

target_link_libraries(
    pubg_native

    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv2-lib}
    ${OpenSLES-lib}
    ${m-lib}
    ${dl-lib}
)
