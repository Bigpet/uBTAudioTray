#pragma once

#include "config.h"
#include <windows.h>
#include <stdbool.h>
#include "app_state.h"
#include "bluetooth.h"

typedef void (*FnDeviceToggle)(const wchar_t* address, const wchar_t* name, bool connect);
typedef void (*FnSettingsRequested)(void);
typedef void (*FnExitRequested)(void);
typedef void (*FnSelectionChanged)(void);

void ui_menu_init(HINSTANCE hInstance, HWND hTrayWnd);
void ui_menu_cleanup(void);

HWND ui_menu_get_hwnd(void);
void ui_menu_show(int anchorX, int anchorY);
void ui_menu_hide(void);
bool ui_menu_is_visible(void);

typedef enum {
    DEVICE_BUSY_NONE = 0,
    DEVICE_BUSY_CONNECTING,
    DEVICE_BUSY_DISCONNECTING,
    DEVICE_BUSY_QUEUED
} DeviceBusyState;

void ui_menu_set_callbacks(FnDeviceToggle onToggle, FnSettingsRequested onSettings, FnExitRequested onExit, FnSelectionChanged onSelectionChanged);
void ui_menu_update_devices(const BluetoothAudioDevice* devices, int count);
void ui_menu_set_busy(bool isBusy);
void ui_menu_set_device_busy(const wchar_t* address, DeviceBusyState state);
void ui_menu_clear_all_busy(void);
DeviceBusyState ui_menu_get_device_busy(const wchar_t* address);

