#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <stdbool.h>
#include "theme.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

typedef enum {
    TASKBAR_EDGE_UNKNOWN,
    TASKBAR_EDGE_LEFT,
    TASKBAR_EDGE_TOP,
    TASKBAR_EDGE_RIGHT,
    TASKBAR_EDGE_BOTTOM
} TaskbarEdge;

TaskbarEdge ui_get_taskbar_edge(POINT pt, RECT* outWorkArea);
void ui_position_window(HWND hwnd, int anchorX, int anchorY, int width, int height);
void ui_enable_rounded_corners(HWND hwnd);

HFONT ui_get_font(int height, bool bold);
void ui_draw_rounded_rect(HDC hdc, const RECT* rc, int radius, COLORREF fillCol, COLORREF borderCol);
void ui_draw_checkbox(HDC hdc, int x, int y, int size, bool isChecked, const ThemeColors* theme);
void ui_draw_radio(HDC hdc, int x, int y, int size, bool isChecked, const ThemeColors* theme);
void ui_draw_status_indicator(HDC hdc, int x, int y, int size, bool isConnected, const ThemeColors* theme);

