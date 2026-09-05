#pragma once

#include <windows.h>
#include <stdbool.h>

typedef struct {
    bool isDark;
    COLORREF bg;
    COLORREF fg;
    COLORREF rowHover;
    COLORREF btnBg;
    COLORREF btnHover;
    COLORREF btnPressed;
    COLORREF btnBorder;
    COLORREF separator;
    COLORREF subtext;
    COLORREF accent;
    COLORREF statusConnected;
    COLORREF statusDisconnected;
    COLORREF checkBg;
    COLORREF checkBorder;
    COLORREF checkMark;
} ThemeColors;

bool theme_is_dark_mode(void);
void theme_get_colors(ThemeColors* colors);

