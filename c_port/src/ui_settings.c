#include "ui_settings.h"
#include "ui_common.h"
#include "startup.h"
#include <shellapi.h>
#include <stdio.h>

#define SETTINGS_WIDTH 280
#define SETTINGS_HEIGHT 255
#define HEADER_HEIGHT 36
#define OPTION_ROW_HEIGHT 28

typedef enum {
    SET_HIT_NONE,
    SET_HIT_GITHUB,
    SET_HIT_NOTIFICATIONS,
    SET_HIT_STARTUP,
    SET_HIT_MEDIA_PLAY,
    SET_HIT_MEDIA_PAUSE,
    SET_HIT_CONNECT_API,
    SET_HIT_CONNECT_UI,
    SET_HIT_DISCONNECT_HCI,
    SET_HIT_DISCONNECT_UI
} SettingsHitType;

static HWND g_hSettingsWnd = NULL;
static HWND g_hTrayWnd = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontSmall = NULL;

static SettingsHitType g_hoveredHit = SET_HIT_NONE;
static bool g_trackingMouse = false;

extern AppState g_appState;

static SettingsHitType hit_test_settings(int x, int y) {
    int width = SETTINGS_WIDTH;

    // Header github link
    if (y >= 6 && y <= 30 && x >= width - 115 && x <= width - 10) {
        return SET_HIT_GITHUB;
    }

    int curY = HEADER_HEIGHT + 4;
    // Row 1: Notifications
    if (y >= curY && y < curY + OPTION_ROW_HEIGHT) return SET_HIT_NOTIFICATIONS;
    curY += OPTION_ROW_HEIGHT;

    // Row 2: Startup
    if (y >= curY && y < curY + OPTION_ROW_HEIGHT) return SET_HIT_STARTUP;
    curY += OPTION_ROW_HEIGHT;

    // Row 3: Play media on Connect
    if (y >= curY && y < curY + OPTION_ROW_HEIGHT) return SET_HIT_MEDIA_PLAY;
    curY += OPTION_ROW_HEIGHT;

    // Row 4: Pause media on Disconnect
    if (y >= curY && y < curY + OPTION_ROW_HEIGHT) return SET_HIT_MEDIA_PAUSE;
    curY += OPTION_ROW_HEIGHT;

    // Separator
    curY += 8;

    // Connect via: API (x: 95..145) / UI (x: 155..205)
    if (y >= curY && y < curY + 26) {
        if (x >= 90 && x <= 145) return SET_HIT_CONNECT_API;
        if (x >= 150 && x <= 205) return SET_HIT_CONNECT_UI;
    }
    curY += 28;

    // Disconnect via: HCI (x: 95..145) / UI (x: 155..205)
    if (y >= curY && y < curY + 26) {
        if (x >= 90 && x <= 145) return SET_HIT_DISCONNECT_HCI;
        if (x >= 150 && x <= 205) return SET_HIT_DISCONNECT_UI;
    }

    return SET_HIT_NONE;
}

static void on_paint_settings(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    ThemeColors theme;
    theme_get_colors(&theme);

    // Background
    HBRUSH bgBrush = CreateSolidBrush(theme.bg);
    FillRect(memDC, &clientRc, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(memDC, TRANSPARENT);

    // 1. Header: "Settings"
    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, theme.fg);
    RECT titleRc = { 12, 6, width - 120, HEADER_HEIGHT };
    DrawTextW(memDC, L"Settings", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // GitHub version link
    RECT gitRc = { width - 120, 6, width - 10, HEADER_HEIGHT };
    if (g_hoveredHit == SET_HIT_GITHUB) {
        ui_draw_rounded_rect(memDC, &gitRc, 4, theme.rowHover, CLR_INVALID);
    }
    SelectObject(memDC, g_hFontSmall);
    SetTextColor(memDC, theme.accent);
    DrawTextW(memDC, L"uBTAudioTray-v1.0", -1, &gitRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Separator
    HPEN sepPen = CreatePen(PS_SOLID, 1, theme.separator);
    HPEN oldPen = (HPEN)SelectObject(memDC, sepPen);
    MoveToEx(memDC, 10, HEADER_HEIGHT + 1, NULL);
    LineTo(memDC, width - 10, HEADER_HEIGHT + 1);

    // 2. Checkboxes
    int curY = HEADER_HEIGHT + 4;
    SelectObject(memDC, g_hFontNormal);
    SetTextColor(memDC, theme.fg);

    // Notifications
    RECT row1Rc = { 6, curY, width - 6, curY + OPTION_ROW_HEIGHT };
    if (g_hoveredHit == SET_HIT_NOTIFICATIONS) ui_draw_rounded_rect(memDC, &row1Rc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, 14, curY + 6, 14, g_appState.enableNotifications, &theme);
    RECT text1Rc = { 36, curY, width - 12, curY + OPTION_ROW_HEIGHT };
    DrawTextW(memDC, L"Enable Notifications", -1, &text1Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += OPTION_ROW_HEIGHT;

    // Start with Windows
    bool runOnStartup = startup_is_enabled();
    RECT row2Rc = { 6, curY, width - 6, curY + OPTION_ROW_HEIGHT };
    if (g_hoveredHit == SET_HIT_STARTUP) ui_draw_rounded_rect(memDC, &row2Rc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, 14, curY + 6, 14, runOnStartup, &theme);
    RECT text2Rc = { 36, curY, width - 12, curY + OPTION_ROW_HEIGHT };
    DrawTextW(memDC, L"Start with Windows", -1, &text2Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += OPTION_ROW_HEIGHT;

    // Play media on Connect
    RECT row3Rc = { 6, curY, width - 6, curY + OPTION_ROW_HEIGHT };
    if (g_hoveredHit == SET_HIT_MEDIA_PLAY) ui_draw_rounded_rect(memDC, &row3Rc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, 14, curY + 6, 14, g_appState.sendMediaPlayOnConnect, &theme);
    RECT text3Rc = { 36, curY, width - 12, curY + OPTION_ROW_HEIGHT };
    DrawTextW(memDC, L"Play media on Connect", -1, &text3Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += OPTION_ROW_HEIGHT;

    // Pause media on Disconnect
    RECT row4Rc = { 6, curY, width - 6, curY + OPTION_ROW_HEIGHT };
    if (g_hoveredHit == SET_HIT_MEDIA_PAUSE) ui_draw_rounded_rect(memDC, &row4Rc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, 14, curY + 6, 14, g_appState.sendMediaPauseOnDisconnect, &theme);
    RECT text4Rc = { 36, curY, width - 12, curY + OPTION_ROW_HEIGHT };
    DrawTextW(memDC, L"Pause media on Disconnect", -1, &text4Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += OPTION_ROW_HEIGHT;

    // Separator
    curY += 4;
    MoveToEx(memDC, 10, curY, NULL);
    LineTo(memDC, width - 10, curY);
    SelectObject(memDC, oldPen);
    DeleteObject(sepPen);
    curY += 4;

    // 3. Connect via:
    RECT connLabelRc = { 12, curY, 86, curY + 24 };
    DrawTextW(memDC, L"Connect via:", -1, &connLabelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // API radio
    RECT apiRc = { 90, curY, 142, curY + 24 };
    if (g_hoveredHit == SET_HIT_CONNECT_API) ui_draw_rounded_rect(memDC, &apiRc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, 95, curY + 5, 14, !g_appState.useUiaConnect, &theme);
    RECT apiTextRc = { 115, curY, 142, curY + 24 };
    DrawTextW(memDC, L"API", -1, &apiTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // UI radio
    RECT uiRc = { 146, curY, 198, curY + 24 };
    if (g_hoveredHit == SET_HIT_CONNECT_UI) ui_draw_rounded_rect(memDC, &uiRc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, 151, curY + 5, 14, g_appState.useUiaConnect, &theme);
    RECT uiTextRc = { 171, curY, 198, curY + 24 };
    DrawTextW(memDC, L"UI", -1, &uiTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    curY += 28;

    // 4. Disconnect via:
    RECT disconnLabelRc = { 12, curY, 96, curY + 24 };
    DrawTextW(memDC, L"Disconnect via:", -1, &disconnLabelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // HCI radio
    RECT hciRc = { 100, curY, 152, curY + 24 };
    if (g_hoveredHit == SET_HIT_DISCONNECT_HCI) ui_draw_rounded_rect(memDC, &hciRc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, 105, curY + 5, 14, g_appState.useHciDisconnect, &theme);
    RECT hciTextRc = { 125, curY, 152, curY + 24 };
    DrawTextW(memDC, L"HCI", -1, &hciTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // UI radio
    RECT ui2Rc = { 156, curY, 208, curY + 24 };
    if (g_hoveredHit == SET_HIT_DISCONNECT_UI) ui_draw_rounded_rect(memDC, &ui2Rc, 4, theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, 161, curY + 5, 14, g_appState.useUiaDisconnect, &theme);
    RECT ui2TextRc = { 181, curY, 208, curY + 24 };
    DrawTextW(memDC, L"UI", -1, &ui2TextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Outer border
    HPEN outPen = CreatePen(PS_SOLID, 1, theme.separator);
    oldPen = (HPEN)SelectObject(memDC, outPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldB = (HBRUSH)SelectObject(memDC, nullBrush);
    RoundRect(memDC, 0, 0, width, height, 8, 8);
    SelectObject(memDC, oldB);
    SelectObject(memDC, oldPen);
    DeleteObject(outPen);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK settings_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            on_paint_settings(hwnd);
            return 0;

        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);

            if (!g_trackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                g_trackingMouse = true;
            }

            SettingsHitType hit = hit_test_settings(x, y);
            if (hit != g_hoveredHit) {
                g_hoveredHit = hit;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_trackingMouse = false;
            g_hoveredHit = SET_HIT_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            SettingsHitType hit = hit_test_settings(x, y);

            if (hit == SET_HIT_GITHUB) {
                ShellExecuteW(NULL, L"open", L"https://github.com/QuickBTTray/QuickBTTray", NULL, NULL, SW_SHOWNORMAL);
            } else if (hit == SET_HIT_NOTIFICATIONS) {
                g_appState.enableNotifications = !g_appState.enableNotifications;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_STARTUP) {
                bool enabled = startup_is_enabled();
                startup_set_enabled(!enabled);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_MEDIA_PLAY) {
                g_appState.sendMediaPlayOnConnect = !g_appState.sendMediaPlayOnConnect;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_MEDIA_PAUSE) {
                g_appState.sendMediaPauseOnDisconnect = !g_appState.sendMediaPauseOnDisconnect;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_CONNECT_API) {
                g_appState.useUiaConnect = false;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_CONNECT_UI) {
                g_appState.useUiaConnect = true;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_DISCONNECT_HCI) {
                g_appState.useHciDisconnect = true;
                g_appState.useUiaDisconnect = false;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == SET_HIT_DISCONNECT_UI) {
                g_appState.useHciDisconnect = false;
                g_appState.useUiaDisconnect = true;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                ui_settings_hide();
            }
            return 0;

        case WM_SETCURSOR:
            if (g_hoveredHit != SET_HIT_NONE) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ui_settings_init(HINSTANCE hInstance, HWND hTrayWnd) {
    g_hTrayWnd = hTrayWnd;

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = settings_wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"uBTAudioTray_SettingsWindow";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"uBTAudioTray Settings",
        WS_POPUP,
        0, 0, SETTINGS_WIDTH, SETTINGS_HEIGHT,
        NULL, NULL, hInstance, NULL);

    ui_enable_rounded_corners(g_hSettingsWnd);

    g_hFontNormal = ui_get_font(-12, false);
    g_hFontBold   = ui_get_font(-12, true);
    g_hFontSmall  = ui_get_font(-11, false);
}

void ui_settings_cleanup(void) {
    if (g_hSettingsWnd) {
        DestroyWindow(g_hSettingsWnd);
        g_hSettingsWnd = NULL;
    }
    if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
    if (g_hFontBold)   { DeleteObject(g_hFontBold);   g_hFontBold = NULL; }
    if (g_hFontSmall)  { DeleteObject(g_hFontSmall);  g_hFontSmall = NULL; }
}

HWND ui_settings_get_hwnd(void) {
    return g_hSettingsWnd;
}

void ui_settings_show(int anchorX, int anchorY) {
    if (!g_hSettingsWnd) return;

    ui_position_window(g_hSettingsWnd, anchorX, anchorY, SETTINGS_WIDTH, SETTINGS_HEIGHT);
    ShowWindow(g_hSettingsWnd, SW_SHOW);
    SetForegroundWindow(g_hSettingsWnd);
    InvalidateRect(g_hSettingsWnd, NULL, TRUE);
}

void ui_settings_hide(void) {
    if (g_hSettingsWnd && IsWindowVisible(g_hSettingsWnd)) {
        ShowWindow(g_hSettingsWnd, SW_HIDE);
    }
}

bool ui_settings_is_visible(void) {
    return g_hSettingsWnd ? IsWindowVisible(g_hSettingsWnd) : false;
}

