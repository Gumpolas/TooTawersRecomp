# Xbox Input → XInput Compatibility Layer

add_library(xbox_input STATIC
    xinput_device.c
    input_bind.c
)

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    # XInput is loaded at run time (xinput1_4 / 1_3 / 9_1_0), and SDL2.dll,
    # for PlayStation and other non-Xbox pads, too when it is beside the
    # game: only SDL's headers are needed to build (vendored).
    target_link_libraries(xbox_input PUBLIC platform)
    target_include_directories(xbox_input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL2/include)
    target_compile_definitions(xbox_input PRIVATE XBOX_HAVE_SDL2=1)
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(xbox_input PUBLIC platform SDL2::SDL2)
endif()

if(MSVC)
    target_compile_options(xbox_input PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
endif()
