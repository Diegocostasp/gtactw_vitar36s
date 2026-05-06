cmake_minimum_required(VERSION 3.10)
project(gtactw_portmaster C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

option(PORTMASTER_LINUX "Build PortMaster/Linux version" OFF)

if(NOT PORTMASTER_LINUX)
    message(FATAL_ERROR "This CMakeLists is for PortMaster. Use -DPORTMASTER_LINUX=ON.")
endif()

add_definitions(-DPORTMASTER_LINUX=1)

include_directories(
    loader
    platform/portmaster
)

add_executable(gtactw.armhf
    loader/so_util.c

    platform/portmaster/main_pm.c
    platform/portmaster/sdl_pm.c
    platform/portmaster/stubs_pm.c
)

target_link_libraries(gtactw.armhf
    SDL2
    GLESv2
    EGL
    openal
    mpg123
    m
    dl
    pthread
)
