# Shared FMV player: Media Foundation decode onto a D3D8 texture.
add_library(xbox_video STATIC
    video_player.c
    video_pump.c
    fb_present.c
    gfx_menu.c
    host_text.c
)
target_include_directories(xbox_video PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(xbox_video PUBLIC xbox_d3d8)
if(WIN32)
    target_link_libraries(xbox_video PUBLIC mfuuid ole32)   # Media Foundation: loaded at run time (mf_dyn.h)
else()
    # the window, its OpenGL context and keyboard/mouse
    find_package(SDL2 REQUIRED)
    target_link_libraries(xbox_video PUBLIC SDL2::SDL2 platform)
endif()
