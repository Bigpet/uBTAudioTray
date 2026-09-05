cmake_minimum_required(VERSION 3.20)
project(uBTAudioTray C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)

set(SOURCES
    src/main.c
    src/app_state.c
    src/startup.c
    src/theme.c
    src/media.c
    src/bluetooth.c
    src/ui_common.c
    src/ui_menu.c
    src/ui_settings.c
    src/uia_connect.c
    res/resource.rc
)

add_executable(uBTAudioTray WIN32 ${SOURCES})

target_compile_options(uBTAudioTray PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W4 /wd4201 /utf-8 /O2>
)

target_link_libraries(uBTAudioTray PRIVATE
    user32
    gdi32
    shell32
    advapi32
    bthprops
    dwmapi
    comctl32
    ole32
    oleaut32
)

