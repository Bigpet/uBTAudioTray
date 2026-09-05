#pragma once

#include <windows.h>
#include <stdbool.h>
#include "app_state.h"

void ui_settings_init(HINSTANCE hInstance, HWND hTrayWnd);
void ui_settings_cleanup(void);

HWND ui_settings_get_hwnd(void);
void ui_settings_show(void);
void ui_settings_hide(void);
bool ui_settings_is_visible(void);

