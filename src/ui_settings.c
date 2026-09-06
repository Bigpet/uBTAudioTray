#include "config.h"
#include "ui_settings.h"
#include "ui_menu.h"
#include "ui_common.h"
#include "startup.h"
#include <shellapi.h>
#include <stdio.h>

#define BASE_SETTINGS_WIDTH 300
#define BASE_SETTINGS_HEIGHT 255
#define BASE_HEADER_HEIGHT 36
#define BASE_OPTION_ROW_HEIGHT 28

typedef enum {
    SET_HIT_NONE,
    SET_HIT_GITHUB,
    SET_HIT_NOTIFICATIONS,
    SET_HIT_STARTUP,
    SET_HIT_MEDIA_PLAY,
    SET_HIT_MEDIA_PAUSE,
    SET_HIT_CONNECT_KS,
    SET_HIT_CONNECT_API,
    SET_HIT_CONNECT_UI,
    SET_HIT_DISCONNECT_KS,
    SET_HIT_DISCONNECT_HCI,
    SET_HIT_DISCONNECT_UI
} SettingsHitType;

static HWND g_hSettingsWnd = NULL;
static HWND g_hTrayWnd = NULL;
static UINT g_currentDpi = 96;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontSmall = NULL;

static SettingsHitType g_hoveredHit = SET_HIT_NONE;
static bool g_trackingMouse = false;

extern AppState g_appState;

static void update_settings_fonts(UINT dpi) {
    if (g_hFontNormal) DeleteObject(g_hFontNormal);
    if (g_hFontBold)   DeleteObject(g_hFontBold);
    if (g_hFontSmall)  DeleteObject(g_hFontSmall);

    g_hFontNormal = ui_get_font_for_dpi(-12, false, dpi);
    g_hFontBold   = ui_get_font_for_dpi(-12, true, dpi);
    g_hFontSmall  = ui_get_font_for_dpi(-11, false, dpi);
    g_currentDpi = dpi;
}

static SettingsHitType hit_test_settings(int x, int y) {
    UINT dpi = g_currentDpi;
    RECT clientRc;
    GetClientRect(g_hSettingsWnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    if (width <= 0) width = ui_scale(BASE_SETTINGS_WIDTH, dpi);

    int headerH = ui_scale(BASE_HEADER_HEIGHT, dpi);
    int rowH = ui_scale(BASE_OPTION_ROW_HEIGHT, dpi);

    // Header github link
    if (y >= ui_scale(6, dpi) && y <= ui_scale(30, dpi) &&
        x >= width - ui_scale(130, dpi) && x <= width - ui_scale(10, dpi)) {
        return SET_HIT_GITHUB;
    }

    int curY = headerH + ui_scale(4, dpi);
    // Row 1: Notifications
    if (y >= curY && y < curY + rowH) return SET_HIT_NOTIFICATIONS;
    curY += rowH;

    // Row 2: Startup
    if (y >= curY && y < curY + rowH) return SET_HIT_STARTUP;
    curY += rowH;

    // Row 3: Play media on Connect
    if (y >= curY && y < curY + rowH) return SET_HIT_MEDIA_PLAY;
    curY += rowH;

    // Row 4: Pause media on Disconnect
    if (y >= curY && y < curY + rowH) return SET_HIT_MEDIA_PAUSE;
    curY += rowH;

    // Separator
    curY += ui_scale(8, dpi);

    int radioRowH = ui_scale(26, dpi);
    // Connect via:
    if (y >= curY && y < curY + radioRowH) {
        int cx = ui_scale(90, dpi);
#if ENABLE_KS
        if (x >= cx && x <= cx + ui_scale(52, dpi)) return SET_HIT_CONNECT_KS;
        cx += ui_scale(56, dpi);
#endif
#if ENABLE_API_HCI
        if (x >= cx && x <= cx + ui_scale(54, dpi)) return SET_HIT_CONNECT_API;
        cx += ui_scale(58, dpi);
#endif
#if ENABLE_UI
        if (x >= cx && x <= cx + ui_scale(48, dpi)) return SET_HIT_CONNECT_UI;
        cx += ui_scale(52, dpi);
#endif
    }
    curY += ui_scale(28, dpi);

    // Disconnect via:
    if (y >= curY && y < curY + radioRowH) {
        int cx = ui_scale(100, dpi);
#if ENABLE_KS
        if (x >= cx && x <= cx + ui_scale(50, dpi)) return SET_HIT_DISCONNECT_KS;
        cx += ui_scale(54, dpi);
#endif
#if ENABLE_API_HCI
        if (x >= cx && x <= cx + ui_scale(52, dpi)) return SET_HIT_DISCONNECT_HCI;
        cx += ui_scale(56, dpi);
#endif
#if ENABLE_UI
        if (x >= cx && x <= cx + ui_scale(46, dpi)) return SET_HIT_DISCONNECT_UI;
        cx += ui_scale(50, dpi);
#endif
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

    UINT dpi = g_currentDpi;
    int headerH = ui_scale(BASE_HEADER_HEIGHT, dpi);
    int rowH = ui_scale(BASE_OPTION_ROW_HEIGHT, dpi);
    int checkSize = ui_scale(14, dpi);

    // 1. Header: "Settings"
    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, theme.fg);
    RECT titleRc = { ui_scale(12, dpi), ui_scale(6, dpi), width - ui_scale(135, dpi), headerH };
    DrawTextW(memDC, L"Settings", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // GitHub version link
    SelectObject(memDC, g_hFontSmall);
    const wchar_t* verText = L"uBTAudioTray-v" APP_VERSION_WIDE;
    SIZE sz = { 0 };
    GetTextExtentPoint32W(memDC, verText, (int)wcslen(verText), &sz);
    int padX = ui_scale(4, dpi);
    int gitW = sz.cx + padX * 2;
    RECT gitRc = { width - ui_scale(10, dpi) - gitW, ui_scale(6, dpi), width - ui_scale(10, dpi), headerH - ui_scale(6, dpi) };
    if (g_hoveredHit == SET_HIT_GITHUB) {
        ui_draw_rounded_rect(memDC, &gitRc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    }
    SetTextColor(memDC, theme.accent);
    DrawTextW(memDC, verText, -1, &gitRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Separator
    HPEN sepPen = CreatePen(PS_SOLID, 1, theme.separator);
    HPEN oldPen = (HPEN)SelectObject(memDC, sepPen);
    MoveToEx(memDC, ui_scale(10, dpi), headerH + 1, NULL);
    LineTo(memDC, width - ui_scale(10, dpi), headerH + 1);

    // 2. Checkboxes
    int curY = headerH + ui_scale(4, dpi);
    SelectObject(memDC, g_hFontNormal);
    SetTextColor(memDC, theme.fg);

    // Notifications
    RECT row1Rc = { ui_scale(6, dpi), curY, width - ui_scale(6, dpi), curY + rowH };
    if (g_hoveredHit == SET_HIT_NOTIFICATIONS) ui_draw_rounded_rect(memDC, &row1Rc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, ui_scale(14, dpi), curY + ((rowH - checkSize) / 2), checkSize, g_appState.enableNotifications, &theme);
    RECT text1Rc = { ui_scale(36, dpi), curY, width - ui_scale(12, dpi), curY + rowH };
    DrawTextW(memDC, L"Enable Notifications", -1, &text1Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += rowH;

    // Start with Windows
    bool runOnStartup = startup_is_enabled();
    RECT row2Rc = { ui_scale(6, dpi), curY, width - ui_scale(6, dpi), curY + rowH };
    if (g_hoveredHit == SET_HIT_STARTUP) ui_draw_rounded_rect(memDC, &row2Rc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, ui_scale(14, dpi), curY + ((rowH - checkSize) / 2), checkSize, runOnStartup, &theme);
    RECT text2Rc = { ui_scale(36, dpi), curY, width - ui_scale(12, dpi), curY + rowH };
    DrawTextW(memDC, L"Start with Windows", -1, &text2Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += rowH;

    // Play media on Connect
    RECT row3Rc = { ui_scale(6, dpi), curY, width - ui_scale(6, dpi), curY + rowH };
    if (g_hoveredHit == SET_HIT_MEDIA_PLAY) ui_draw_rounded_rect(memDC, &row3Rc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, ui_scale(14, dpi), curY + ((rowH - checkSize) / 2), checkSize, g_appState.sendMediaPlayOnConnect, &theme);
    RECT text3Rc = { ui_scale(36, dpi), curY, width - ui_scale(12, dpi), curY + rowH };
    DrawTextW(memDC, L"Play media on Connect", -1, &text3Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += rowH;

    // Pause media on Disconnect
    RECT row4Rc = { ui_scale(6, dpi), curY, width - ui_scale(6, dpi), curY + rowH };
    if (g_hoveredHit == SET_HIT_MEDIA_PAUSE) ui_draw_rounded_rect(memDC, &row4Rc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_checkbox(memDC, ui_scale(14, dpi), curY + ((rowH - checkSize) / 2), checkSize, g_appState.sendMediaPauseOnDisconnect, &theme);
    RECT text4Rc = { ui_scale(36, dpi), curY, width - ui_scale(12, dpi), curY + rowH };
    DrawTextW(memDC, L"Pause media on Disconnect", -1, &text4Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    curY += rowH;

    // Separator
    curY += ui_scale(4, dpi);
    MoveToEx(memDC, ui_scale(10, dpi), curY, NULL);
    LineTo(memDC, width - ui_scale(10, dpi), curY);
    SelectObject(memDC, oldPen);
    DeleteObject(sepPen);
    curY += ui_scale(4, dpi);

    int radioRowH = ui_scale(24, dpi);

    // 3. Connect via:
    RECT connLabelRc = { ui_scale(12, dpi), curY, ui_scale(86, dpi), curY + radioRowH };
    DrawTextW(memDC, L"Connect via:", -1, &connLabelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int cx = ui_scale(90, dpi);
#if ENABLE_KS
    RECT ksRc = { cx, curY, cx + ui_scale(52, dpi), curY + radioRowH };
    if (g_hoveredHit == SET_HIT_CONNECT_KS) ui_draw_rounded_rect(memDC, &ksRc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, cx + ui_scale(4, dpi), curY + ((radioRowH - checkSize) / 2), checkSize, g_appState.connectMethod == CONNECT_METHOD_KS, &theme);
    RECT ksTextRc = { cx + ui_scale(23, dpi), curY, cx + ui_scale(52, dpi), curY + radioRowH };
    DrawTextW(memDC, L"KS", -1, &ksTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cx += ui_scale(56, dpi);
#endif

#if ENABLE_API_HCI
    RECT apiRc = { cx, curY, cx + ui_scale(54, dpi), curY + radioRowH };
    if (g_hoveredHit == SET_HIT_CONNECT_API) ui_draw_rounded_rect(memDC, &apiRc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, cx + ui_scale(4, dpi), curY + ((radioRowH - checkSize) / 2), checkSize, g_appState.connectMethod == CONNECT_METHOD_API, &theme);
    RECT apiTextRc = { cx + ui_scale(23, dpi), curY, cx + ui_scale(54, dpi), curY + radioRowH };
    DrawTextW(memDC, L"API", -1, &apiTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cx += ui_scale(58, dpi);
#endif

#if ENABLE_UI
    RECT uiRc = { cx, curY, cx + ui_scale(48, dpi), curY + radioRowH };
    if (g_hoveredHit == SET_HIT_CONNECT_UI) ui_draw_rounded_rect(memDC, &uiRc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, cx + ui_scale(4, dpi), curY + ((radioRowH - checkSize) / 2), checkSize, g_appState.connectMethod == CONNECT_METHOD_UI, &theme);
    RECT uiTextRc = { cx + ui_scale(23, dpi), curY, cx + ui_scale(48, dpi), curY + radioRowH };
    DrawTextW(memDC, L"UI", -1, &uiTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cx += ui_scale(52, dpi);
#endif

    curY += ui_scale(28, dpi);

    // 4. Disconnect via:
    RECT disconnLabelRc = { ui_scale(12, dpi), curY, ui_scale(96, dpi), curY + radioRowH };
    DrawTextW(memDC, L"Disconnect via:", -1, &disconnLabelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int dcx = ui_scale(100, dpi);
#if ENABLE_KS
    RECT dksRc = { dcx, curY, dcx + ui_scale(50, dpi), curY + radioRowH };
    if (g_hoveredHit == SET_HIT_DISCONNECT_KS) ui_draw_rounded_rect(memDC, &dksRc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, dcx + ui_scale(4, dpi), curY + ((radioRowH - checkSize) / 2), checkSize, g_appState.disconnectMethod == DISCONNECT_METHOD_KS, &theme);
    RECT dksTextRc = { dcx + ui_scale(23, dpi), curY, dcx + ui_scale(50, dpi), curY + radioRowH };
    DrawTextW(memDC, L"KS", -1, &dksTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    dcx += ui_scale(54, dpi);
#endif

#if ENABLE_API_HCI
    RECT hciRc = { dcx, curY, dcx + ui_scale(52, dpi), curY + radioRowH };
    if (g_hoveredHit == SET_HIT_DISCONNECT_HCI) ui_draw_rounded_rect(memDC, &hciRc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, dcx + ui_scale(4, dpi), curY + ((radioRowH - checkSize) / 2), checkSize, g_appState.disconnectMethod == DISCONNECT_METHOD_HCI, &theme);
    RECT hciTextRc = { dcx + ui_scale(23, dpi), curY, dcx + ui_scale(52, dpi), curY + radioRowH };
    DrawTextW(memDC, L"HCI", -1, &hciTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    dcx += ui_scale(56, dpi);
#endif

#if ENABLE_UI
    RECT ui2Rc = { dcx, curY, dcx + ui_scale(46, dpi), curY + radioRowH };
    if (g_hoveredHit == SET_HIT_DISCONNECT_UI) ui_draw_rounded_rect(memDC, &ui2Rc, ui_scale(4, dpi), theme.rowHover, CLR_INVALID);
    ui_draw_radio(memDC, dcx + ui_scale(4, dpi), curY + ((radioRowH - checkSize) / 2), checkSize, g_appState.disconnectMethod == DISCONNECT_METHOD_UI, &theme);
    RECT ui2TextRc = { dcx + ui_scale(23, dpi), curY, dcx + ui_scale(46, dpi), curY + radioRowH };
    DrawTextW(memDC, L"UI", -1, &ui2TextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    dcx += ui_scale(50, dpi);
#endif

    // Outer border
    HPEN outPen = CreatePen(PS_SOLID, 1, theme.separator);
    oldPen = (HPEN)SelectObject(memDC, outPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldB = (HBRUSH)SelectObject(memDC, nullBrush);
    RoundRect(memDC, 0, 0, width, height, ui_scale(8, dpi), ui_scale(8, dpi));
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
        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            update_settings_fonts(newDpi);
            RECT* prc = (RECT*)lParam;
            if (prc) {
                SetWindowPos(hwnd, NULL, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

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
                ShellExecuteW(NULL, L"open", L"https://github.com/Bigpet/uBTAudioTray", NULL, NULL, SW_SHOWNORMAL);
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
#if ENABLE_KS
            } else if (hit == SET_HIT_CONNECT_KS) {
                g_appState.connectMethod = CONNECT_METHOD_KS;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
#endif
#if ENABLE_API_HCI
            } else if (hit == SET_HIT_CONNECT_API) {
                g_appState.connectMethod = CONNECT_METHOD_API;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
#endif
#if ENABLE_UI
            } else if (hit == SET_HIT_CONNECT_UI) {
                g_appState.connectMethod = CONNECT_METHOD_UI;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
#endif
#if ENABLE_KS
            } else if (hit == SET_HIT_DISCONNECT_KS) {
                g_appState.disconnectMethod = DISCONNECT_METHOD_KS;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
#endif
#if ENABLE_API_HCI
            } else if (hit == SET_HIT_DISCONNECT_HCI) {
                g_appState.disconnectMethod = DISCONNECT_METHOD_HCI;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
#endif
#if ENABLE_UI
            } else if (hit == SET_HIT_DISCONNECT_UI) {
                g_appState.disconnectMethod = DISCONNECT_METHOD_UI;
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
#endif
            }
            return 0;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                HWND hActivating = (HWND)lParam;
                if (hActivating != ui_menu_get_hwnd()) {
                    ui_settings_hide();
                    ui_menu_hide();
                }
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
        0, 0, BASE_SETTINGS_WIDTH, BASE_SETTINGS_HEIGHT,
        NULL, NULL, hInstance, NULL);

    ui_enable_rounded_corners(g_hSettingsWnd);

    update_settings_fonts(ui_get_window_dpi(g_hSettingsWnd));
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

void ui_settings_show(void) {
    if (!g_hSettingsWnd) return;

    HWND hMenu = ui_menu_get_hwnd();
    if (!hMenu || !IsWindowVisible(hMenu)) return;

    RECT menuRc = { 0 };
    GetWindowRect(hMenu, &menuRc);

    POINT pt = { menuRc.left, menuRc.top };
    UINT dpi = ui_get_point_dpi(pt);
    if (dpi != g_currentDpi || !g_hFontNormal) {
        update_settings_fonts(dpi);
    }

    int settingsW = ui_scale(BASE_SETTINGS_WIDTH, dpi);
    int settingsH = ui_scale(BASE_SETTINGS_HEIGHT, dpi);
    int pad6 = ui_scale(6, dpi);

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { 0 };
    mi.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoW(hMon, &mi);
    RECT work = mi.rcWork;

    int left = menuRc.left;
    int top = menuRc.top - settingsH - pad6;

    // If not enough room directly above Menu, try to the left of Menu
    if (top < work.top + pad6) {
        if (menuRc.left - settingsW - pad6 >= work.left + pad6) {
            left = menuRc.left - settingsW - pad6;
            top = menuRc.bottom - settingsH;
        } else {
            top = work.top + pad6;
        }
    }

    // Clamp within work area bounds
    if (left < work.left + pad6) left = work.left + pad6;
    if (left + settingsW > work.right - pad6) left = work.right - settingsW - pad6;
    if (top < work.top + pad6) top = work.top + pad6;
    if (top + settingsH > work.bottom - pad6) top = work.bottom - settingsH - pad6;

    SetWindowPos(g_hSettingsWnd, HWND_TOPMOST, left, top, settingsW, settingsH, SWP_SHOWWINDOW);
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


