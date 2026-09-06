cmake_minimum_required(VERSION 3.20)
project(uBTAudioTray VERSION 1.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_KS "Enable Kernel Streaming (KS) connection method" ON)
option(ENABLE_API_HCI "Enable Win32 API / HCI connection method" ON)
option(ENABLE_UI "Enable Windows Settings UI Automation connection method" ON)

add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)

if(ENABLE_KS)
    add_definitions(-DENABLE_KS=1)
else()
    add_definitions(-DENABLE_KS=0)
endif()

if(ENABLE_API_HCI)
    add_definitions(-DENABLE_API_HCI=1)
else()
    add_definitions(-DENABLE_API_HCI=0)
endif()

if(ENABLE_UI)
    add_definitions(-DENABLE_UI=1)
else()
    add_definitions(-DENABLE_UI=0)
endif()

set(SOURCES
    src/main.c
    src/app_state.c
    src/startup.c
    src/theme.c
    src/media.c
    src/bluetooth.c
    src/audio_state_notification.c
    src/ui_common.c
    src/ui_menu.c
    src/ui_settings.c
    res/resource.rc
)

if(ENABLE_UI)
    list(APPEND SOURCES src/uia_connect.c)
endif()

add_executable(uBTAudioTray WIN32 ${SOURCES})

if(MSVC)
    target_compile_options(uBTAudioTray PRIVATE /W4 /wd4201 /utf-8 /O2)
    target_link_options(uBTAudioTray PRIVATE "/MANIFEST:NO")
else()
    target_compile_options(uBTAudioTray PRIVATE -Wall -O2)
endif()

target_link_libraries(uBTAudioTray PRIVATE
    user32
    gdi32
    shell32
    advapi32
    bthprops
    dwmapi
    comctl32
    ole32
    gdiplus
)

if(ENABLE_UI)
    target_link_libraries(uBTAudioTray PRIVATE oleaut32)
endif()
