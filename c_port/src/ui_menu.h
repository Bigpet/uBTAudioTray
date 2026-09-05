#pragma once

#include <windows.h>
#include <stdbool.h>
#include "app_state.h"
#include "bluetooth.h"

typedef void (*FnDeviceToggle)(const wchar_t* address, const wchar_t* name, bool connect);
typedef void (*FnSettingsRequested)(void);
typedef void (*FnExitRequested)(void);

void ui_menu_init(HINSTANCE hInstance, HWND hTrayWnd);
void ui_menu_cleanup(void);

HWND ui_menu_get_hwnd(void);
void ui_menu_show(int anchorX, int anchorY);
void ui_menu_hide(void);
bool ui_menu_is_visible(void);

void ui_menu_set_callbacks(FnDeviceToggle onToggle, FnSettingsRequested onSettings, FnExitRequested onExit);
void ui_menu_update_devices(const BluetoothAudioDevice* devices, int count);
void ui_menu_set_busy(bool isBusy);

