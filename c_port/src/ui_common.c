#include "ui_common.h"
#include <math.h>

TaskbarEdge ui_get_taskbar_edge(POINT pt, RECT* outWorkArea) {
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(hMon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, outWorkArea, 0);
        return TASKBAR_EDGE_BOTTOM;
    }

    if (outWorkArea) {
        *outWorkArea = mi.rcWork;
    }

    if (mi.rcWork.left > mi.rcMonitor.left) return TASKBAR_EDGE_LEFT;
    if (mi.rcWork.top > mi.rcMonitor.top) return TASKBAR_EDGE_TOP;
    if (mi.rcWork.right < mi.rcMonitor.right) return TASKBAR_EDGE_RIGHT;
    if (mi.rcWork.bottom < mi.rcMonitor.bottom) return TASKBAR_EDGE_BOTTOM;
    return TASKBAR_EDGE_BOTTOM;
}

void ui_position_window(HWND hwnd, int anchorX, int anchorY, int width, int height) {
    POINT pt = { anchorX, anchorY };
    RECT workArea = { 0 };
    TaskbarEdge edge = ui_get_taskbar_edge(pt, &workArea);

    int left = 0;
    int top = 0;

    switch (edge) {
        case TASKBAR_EDGE_TOP:
            left = anchorX - (width / 2);
            top = anchorY + 8;
            break;
        case TASKBAR_EDGE_LEFT:
            left = anchorX + 8;
            top = anchorY - (height / 2);
            break;
        case TASKBAR_EDGE_RIGHT:
            left = anchorX - width - 8;
            top = anchorY - (height / 2);
            break;
        case TASKBAR_EDGE_BOTTOM:
        case TASKBAR_EDGE_UNKNOWN:
        default:
            left = anchorX - (width / 2);
            top = anchorY - height - 8;
            break;
    }

    // Clamp to monitor work area
    if (left < workArea.left + 6) left = workArea.left + 6;
    if (left + width > workArea.right - 6) left = workArea.right - width - 6;
    if (top < workArea.top + 6) top = workArea.top + 6;
    if (top + height > workArea.bottom - 6) top = workArea.bottom - height - 6;

    SetWindowPos(hwnd, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);
}

void ui_enable_rounded_corners(HWND hwnd) {
    DWORD preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

HFONT ui_get_font(int height, bool bold) {
    return CreateFontW(
        height, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

void ui_draw_rounded_rect(HDC hdc, const RECT* rc, int radius, COLORREF fillCol, COLORREF borderCol) {
    HBRUSH hBrush = CreateSolidBrush(fillCol);
    HPEN hPen = (borderCol != CLR_INVALID) ? CreatePen(PS_SOLID, 1, borderCol) : (HPEN)GetStockObject(NULL_PEN);

    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);

    DeleteObject(hBrush);
    if (borderCol != CLR_INVALID) DeleteObject(hPen);
}

void ui_draw_checkbox(HDC hdc, int x, int y, int size, bool isChecked, const ThemeColors* theme) {
    RECT rc = { x, y, x + size, y + size };
    ui_draw_rounded_rect(hdc, &rc, 3, theme->checkBg, theme->checkBorder);

    if (isChecked) {
        HPEN hPen = CreatePen(PS_SOLID, 2, theme->checkMark);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        // Checkmark geometry
        int leftX = x + 3;
        int leftY = y + (size / 2);
        int midX  = x + (size * 2 / 5);
        int midY  = y + size - 4;
        int rightX = x + size - 3;
        int rightY = y + 3;

        MoveToEx(hdc, leftX, leftY, NULL);
        LineTo(hdc, midX, midY);
        LineTo(hdc, rightX, rightY);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
    }
}

void ui_draw_radio(HDC hdc, int x, int y, int size, bool isChecked, const ThemeColors* theme) {
    HBRUSH hBrush = CreateSolidBrush(theme->checkBg);
    HPEN hPen = CreatePen(PS_SOLID, 1, theme->checkBorder);

    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    Ellipse(hdc, x, y, x + size, y + size);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    if (isChecked) {
        int pad = size / 4;
        HBRUSH hDotBrush = CreateSolidBrush(theme->checkMark);
        HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);

        hOldBrush = (HBRUSH)SelectObject(hdc, hDotBrush);
        hOldPen = (HPEN)SelectObject(hdc, hNullPen);

        Ellipse(hdc, x + pad, y + pad, x + size - pad, y + size - pad);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hDotBrush);
    }
}

void ui_draw_status_indicator(HDC hdc, int x, int y, int size, bool isConnected, const ThemeColors* theme) {
    COLORREF col = isConnected ? theme->statusConnected : theme->statusDisconnected;
    COLORREF borderCol = isConnected
        ? (theme->isDark ? RGB(35, 160, 90) : RGB(30, 140, 75))
        : (theme->isDark ? RGB(90, 90, 90) : RGB(180, 180, 180));

    HBRUSH hBrush = CreateSolidBrush(col);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);

    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    Ellipse(hdc, x, y, x + size, y + size);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

