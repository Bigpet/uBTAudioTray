#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_common.h"
#include <shellapi.h>
#include <stdio.h>
#include <math.h>

#define WINDOW_WIDTH 300
#define HEADER_HEIGHT 38
#define ROW_HEIGHT 36
#define FOOTER_HEIGHT 34
#define TIMER_SPINNER 2001

typedef enum {
    HIT_NONE,
    HIT_GEAR,
    HIT_EXIT,
    HIT_DEVICE_CHECK,
    HIT_DEVICE_BTN,
    HIT_SETTINGS_LINK
} HitType;

typedef struct {
    HitType type;
    int index;
} HitTestResult;

typedef struct {
    wchar_t address[24];
    DeviceBusyState state;
} DeviceBusyEntry;

static HWND g_hMenuWnd = NULL;
static HWND g_hTrayWnd = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontGear = NULL;

static BluetoothAudioDevice g_devices[MAX_BT_DEVICES];
static int g_deviceCount = 0;
static bool g_isBusy = false;

static DeviceBusyEntry g_busyDevices[MAX_BT_DEVICES];
static int g_busyCount = 0;
static int g_spinnerFrame = 0;

static FnDeviceToggle g_fnToggle = NULL;
static FnSettingsRequested g_fnSettings = NULL;
static FnExitRequested g_fnExit = NULL;

static HitTestResult g_hoveredHit = { HIT_NONE, -1 };
static bool g_trackingMouse = false;
static bool g_suppressDeactivate = false;

extern AppState g_appState;

static int calculate_window_height(void) {
    int rows = (g_deviceCount > 0) ? g_deviceCount : 1;
    return HEADER_HEIGHT + 6 + (rows * ROW_HEIGHT) + 8 + FOOTER_HEIGHT + 8;
}

static HitTestResult hit_test(int x, int y) {
    HitTestResult res = { HIT_NONE, -1 };
    int width = WINDOW_WIDTH;

    // Header buttons
    if (y >= 4 && y <= HEADER_HEIGHT) {
        // Exit button: rightmost (width ~46px)
        if (x >= width - 56 && x <= width - 10) {
            res.type = HIT_EXIT;
            return res;
        }
        // Gear button: ~width - 88 .. width - 60
        if (x >= width - 88 && x <= width - 60) {
            res.type = HIT_GEAR;
            return res;
        }
    }

    int devStartY = HEADER_HEIGHT + 6;
    if (g_deviceCount > 0) {
        for (int i = 0; i < g_deviceCount; i++) {
            int top = devStartY + (i * ROW_HEIGHT);
            int bottom = top + ROW_HEIGHT;
            if (y >= top && y < bottom) {
                // Action button: x: width - 88 .. width - 10
                if (x >= width - 88 && x <= width - 10) {
                    res.type = HIT_DEVICE_BTN;
                    res.index = i;
                    return res;
                }
                // Checkbox & device name: x: 6 .. width - 96
                if (x >= 6 && x < width - 96) {
                    res.type = HIT_DEVICE_CHECK;
                    res.index = i;
                    return res;
                }
            }
        }
    }

    // Footer settings link
    int footerTop = devStartY + ((g_deviceCount > 0 ? g_deviceCount : 1) * ROW_HEIGHT) + 8;
    if (y >= footerTop && y <= footerTop + FOOTER_HEIGHT) {
        if (x >= 6 && x <= width - 6) {
            res.type = HIT_SETTINGS_LINK;
            return res;
        }
    }

    return res;
}

static void on_paint(HWND hwnd) {
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

    // 1. Header: "BT Audio Devices"
    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, theme.fg);
    RECT titleRc = { 12, 8, width - 100, HEADER_HEIGHT };
    DrawTextW(memDC, L"BT Audio Devices", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Header Spinner (when any action is busy)
    if (g_isBusy) {
        SIZE textSize;
        GetTextExtentPoint32W(memDC, L"BT Audio Devices", 16, &textSize);
        int cx = 12 + textSize.cx + 12;
        int cy = HEADER_HEIGHT / 2 + 1;
        double r = 5.5;
        for (int k = 0; k < 8; k++) {
            double angle = k * (3.141592653589793 / 4.0);
            int px = cx + (int)(r * cos(angle) + 0.5);
            int py = cy + (int)(r * sin(angle) + 0.5);
            int dist = (k - g_spinnerFrame + 8) % 8;
            COLORREF dotCol;
            if (dist == 0) {
                dotCol = theme.accent;
            } else if (dist == 7 || dist == 6) {
                dotCol = theme.subtext;
            } else {
                dotCol = theme.separator;
            }
            HBRUSH hDotBrush = CreateSolidBrush(dotCol);
            HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hDotBrush);
            HPEN hOldP = (HPEN)SelectObject(memDC, hNullPen);
            Ellipse(memDC, px - 1, py - 1, px + 2, py + 2);
            SelectObject(memDC, hOldB);
            SelectObject(memDC, hOldP);
            DeleteObject(hDotBrush);
        }
    }

    // Gear Icon Button
    RECT gearRc = { width - 88, 5, width - 60, 33 };
    if (g_hoveredHit.type == HIT_GEAR) {
        ui_draw_rounded_rect(memDC, &gearRc, 4, theme.rowHover, theme.separator);
    }
    SelectObject(memDC, g_hFontGear);
    DrawTextW(memDC, L"\u2699", -1, &gearRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Exit Button
    RECT exitRc = { width - 56, 7, width - 10, 31 };
    COLORREF exitBg = (g_hoveredHit.type == HIT_EXIT) ? theme.btnHover : theme.btnBg;
    ui_draw_rounded_rect(memDC, &exitRc, 4, exitBg, theme.btnBorder);
    SelectObject(memDC, g_hFontNormal);
    SetTextColor(memDC, theme.fg);
    DrawTextW(memDC, L"Exit", -1, &exitRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Header Separator
    HPEN sepPen = CreatePen(PS_SOLID, 1, theme.separator);
    HPEN oldPen = (HPEN)SelectObject(memDC, sepPen);
    MoveToEx(memDC, 10, HEADER_HEIGHT + 2, NULL);
    LineTo(memDC, width - 10, HEADER_HEIGHT + 2);

    // 2. Devices
    int curY = HEADER_HEIGHT + 6;
    if (g_deviceCount == 0) {
        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, theme.subtext);
        RECT emptyRc = { 14, curY, width - 14, curY + ROW_HEIGHT };
        DrawTextW(memDC, L"No Bluetooth Audio Devices found!", -1, &emptyRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        curY += ROW_HEIGHT;
    } else {
        for (int i = 0; i < g_deviceCount; i++) {
            BluetoothAudioDevice* dev = &g_devices[i];
            int rowTop = curY + (i * ROW_HEIGHT);

            DeviceBusyState busy = ui_menu_get_device_busy(dev->address);
            bool isBusyAction = (busy != DEVICE_BUSY_NONE);

            // Row hover background on checkbox + text
            RECT checkTextRc = { 6, rowTop + 2, width - 96, rowTop + ROW_HEIGHT - 2 };
            if (g_hoveredHit.type == HIT_DEVICE_CHECK && g_hoveredHit.index == i) {
                ui_draw_rounded_rect(memDC, &checkTextRc, 4, theme.rowHover, CLR_INVALID);
            }

            // Checkbox
            bool isSel = app_state_is_selected(&g_appState, dev->address);
            ui_draw_checkbox(memDC, 12, rowTop + ((ROW_HEIGHT - 14) / 2), 14, isSel, &theme);

            // Device Name
            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, theme.fg);
            RECT nameRc = { 34, rowTop, width - 120, rowTop + ROW_HEIGHT };
            DrawTextW(memDC, dev->displayName, -1, &nameRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Connection Status indicator (dot) - 13px
            // If connecting, pulse status indicator between disconnected and connected colors!
            bool showConnected = dev->isConnected;
            if (busy == DEVICE_BUSY_CONNECTING) {
                showConnected = (g_spinnerFrame % 2 == 0);
            }
            ui_draw_status_indicator(memDC, width - 112, rowTop + ((ROW_HEIGHT - 13) / 2), 13, showConnected, &theme);

            // Action Button
            const wchar_t* btnLabel;
            if (busy == DEVICE_BUSY_CONNECTING) {
                btnLabel = L"Connecting...";
            } else if (busy == DEVICE_BUSY_DISCONNECTING) {
                btnLabel = L"Disconnecting...";
            } else if (busy == DEVICE_BUSY_QUEUED) {
                btnLabel = L"Queued...";
            } else {
                btnLabel = dev->isConnected ? L"Disconnect" : L"Connect";
            }

            RECT btnRc = { width - 88, rowTop + 4, width - 10, rowTop + ROW_HEIGHT - 4 };
            bool isBtnHover = (!isBusyAction && g_hoveredHit.type == HIT_DEVICE_BTN && g_hoveredHit.index == i);
            COLORREF btnBg = isBtnHover ? theme.btnHover : theme.btnBg;
            ui_draw_rounded_rect(memDC, &btnRc, 4, btnBg, theme.btnBorder);

            SelectObject(memDC, g_hFontSmall);
            SetTextColor(memDC, isBusyAction ? theme.subtext : theme.fg);
            DrawTextW(memDC, btnLabel, -1, &btnRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        curY += g_deviceCount * ROW_HEIGHT;
    }

    // Separator above footer
    curY += 4;
    MoveToEx(memDC, 10, curY, NULL);
    LineTo(memDC, width - 10, curY);
    SelectObject(memDC, oldPen);
    DeleteObject(sepPen);

    // 3. Footer link: "Open Bluetooth & Devices Settings"
    curY += 4;
    RECT footerRc = { 6, curY, width - 6, curY + FOOTER_HEIGHT };
    if (g_hoveredHit.type == HIT_SETTINGS_LINK) {
        ui_draw_rounded_rect(memDC, &footerRc, 4, theme.rowHover, CLR_INVALID);
    }
    SelectObject(memDC, g_hFontNormal);
    SetTextColor(memDC, theme.fg);
    RECT footerTextRc = { 12, curY, width - 12, curY + FOOTER_HEIGHT };
    DrawTextW(memDC, L"Open Bluetooth & Devices Settings", -1, &footerTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Outer subtle border
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

static LRESULT CALLBACK menu_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            on_paint(hwnd);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_SPINNER) {
                g_spinnerFrame = (g_spinnerFrame + 1) % 8;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);

            if (!g_trackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                g_trackingMouse = true;
            }

            HitTestResult hit = hit_test(x, y);
            if (hit.type != g_hoveredHit.type || hit.index != g_hoveredHit.index) {
                g_hoveredHit = hit;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_trackingMouse = false;
            g_hoveredHit.type = HIT_NONE;
            g_hoveredHit.index = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            HitTestResult hit = hit_test(x, y);

            if (hit.type == HIT_EXIT) {
                if (g_fnExit) g_fnExit();
            } else if (hit.type == HIT_GEAR) {
                DWORD now = GetTickCount();
                if (ui_settings_is_visible()) {
                    ui_settings_hide();
                } else if (now - ui_settings_get_last_hide_tick() < 250) {
                    // Closed via deactivation from this click; stay closed
                } else {
                    g_suppressDeactivate = true;
                    if (g_fnSettings) g_fnSettings();
                }
            } else if (hit.type == HIT_SETTINGS_LINK) {
                ShellExecuteW(NULL, L"open", L"ms-settings:bluetooth", NULL, NULL, SW_SHOWNORMAL);
                ui_menu_hide();
            } else if (hit.type == HIT_DEVICE_CHECK && hit.index >= 0 && hit.index < g_deviceCount) {
                BluetoothAudioDevice* dev = &g_devices[hit.index];
                bool currentSel = app_state_is_selected(&g_appState, dev->address);
                app_state_set_selected(&g_appState, dev->address, !currentSel);
                app_state_save(&g_appState);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit.type == HIT_DEVICE_BTN && hit.index >= 0 && hit.index < g_deviceCount) {
                BluetoothAudioDevice* dev = &g_devices[hit.index];
                if (ui_menu_get_device_busy(dev->address) == DEVICE_BUSY_NONE && g_fnToggle) {
                    g_fnToggle(dev->address, dev->name, !dev->isConnected);
                }
            }
            return 0;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                HWND hActivating = (HWND)lParam;
                HWND hSettings = ui_settings_get_hwnd();
                if (!g_suppressDeactivate && hActivating != hSettings) {
                    ui_menu_hide();
                }
                g_suppressDeactivate = false;
            }
            return 0;

        case WM_SETCURSOR:
            if (g_hoveredHit.type == HIT_DEVICE_BTN && g_hoveredHit.index >= 0 && g_hoveredHit.index < g_deviceCount) {
                if (ui_menu_get_device_busy(g_devices[g_hoveredHit.index].address) != DEVICE_BUSY_NONE) {
                    SetCursor(LoadCursorW(NULL, IDC_ARROW));
                    return TRUE;
                }
            }
            if (g_hoveredHit.type != HIT_NONE) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ui_menu_init(HINSTANCE hInstance, HWND hTrayWnd) {
    g_hTrayWnd = hTrayWnd;

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = menu_wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"uBTAudioTray_MenuWindow";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    g_hMenuWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"uBTAudioTray Menu",
        WS_POPUP,
        0, 0, WINDOW_WIDTH, 100,
        NULL, NULL, hInstance, NULL);

    ui_enable_rounded_corners(g_hMenuWnd);

    g_hFontNormal = ui_get_font(-12, false);
    g_hFontBold   = ui_get_font(-12, true);
    g_hFontSmall  = ui_get_font(-11, false);
    g_hFontGear   = CreateFontW(
        -16, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI Symbol");
}

void ui_menu_cleanup(void) {
    if (g_hMenuWnd) {
        KillTimer(g_hMenuWnd, TIMER_SPINNER);
        DestroyWindow(g_hMenuWnd);
        g_hMenuWnd = NULL;
    }
    if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
    if (g_hFontBold)   { DeleteObject(g_hFontBold);   g_hFontBold = NULL; }
    if (g_hFontSmall)  { DeleteObject(g_hFontSmall);  g_hFontSmall = NULL; }
    if (g_hFontGear)   { DeleteObject(g_hFontGear);   g_hFontGear = NULL; }
}

HWND ui_menu_get_hwnd(void) {
    return g_hMenuWnd;
}

void ui_menu_show(int anchorX, int anchorY) {
    if (!g_hMenuWnd) return;

    int height = calculate_window_height();
    ui_position_window(g_hMenuWnd, anchorX, anchorY, WINDOW_WIDTH, height);

    if (g_isBusy) {
        SetTimer(g_hMenuWnd, TIMER_SPINNER, 100, NULL);
    }

    ShowWindow(g_hMenuWnd, SW_SHOW);
    SetForegroundWindow(g_hMenuWnd);
    InvalidateRect(g_hMenuWnd, NULL, TRUE);
}

void ui_menu_hide(void) {
    if (g_hMenuWnd && IsWindowVisible(g_hMenuWnd)) {
        KillTimer(g_hMenuWnd, TIMER_SPINNER);
        ShowWindow(g_hMenuWnd, SW_HIDE);
    }
    ui_settings_hide();
}

bool ui_menu_is_visible(void) {
    return g_hMenuWnd ? IsWindowVisible(g_hMenuWnd) : false;
}

void ui_menu_set_callbacks(FnDeviceToggle onToggle, FnSettingsRequested onSettings, FnExitRequested onExit) {
    g_fnToggle = onToggle;
    g_fnSettings = onSettings;
    g_fnExit = onExit;
}

void ui_menu_update_devices(const BluetoothAudioDevice* devices, int count) {
    if (count > MAX_BT_DEVICES) count = MAX_BT_DEVICES;
    g_deviceCount = count;
    if (devices && count > 0) {
        memcpy(g_devices, devices, count * sizeof(BluetoothAudioDevice));
    }
    if (g_hMenuWnd && IsWindowVisible(g_hMenuWnd)) {
        RECT rc;
        GetWindowRect(g_hMenuWnd, &rc);
        int height = calculate_window_height();
        SetWindowPos(g_hMenuWnd, HWND_TOPMOST, rc.left, rc.top, WINDOW_WIDTH, height, SWP_NOACTIVATE);
        InvalidateRect(g_hMenuWnd, NULL, TRUE);
    }
}

void ui_menu_set_busy(bool isBusy) {
    g_isBusy = isBusy;
    if (g_hMenuWnd) {
        if (isBusy && IsWindowVisible(g_hMenuWnd)) {
            SetTimer(g_hMenuWnd, TIMER_SPINNER, 100, NULL);
        } else if (!isBusy) {
            KillTimer(g_hMenuWnd, TIMER_SPINNER);
        }
        if (IsWindowVisible(g_hMenuWnd)) {
            InvalidateRect(g_hMenuWnd, NULL, FALSE);
        }
    }
}

DeviceBusyState ui_menu_get_device_busy(const wchar_t* address) {
    if (!address || !address[0]) return DEVICE_BUSY_NONE;
    for (int i = 0; i < g_busyCount; i++) {
        if (_wcsicmp(g_busyDevices[i].address, address) == 0) {
            return g_busyDevices[i].state;
        }
    }
    return DEVICE_BUSY_NONE;
}

void ui_menu_set_device_busy(const wchar_t* address, DeviceBusyState state) {
    if (!address || !address[0]) return;

    int existingIdx = -1;
    for (int i = 0; i < g_busyCount; i++) {
        if (_wcsicmp(g_busyDevices[i].address, address) == 0) {
            existingIdx = i;
            break;
        }
    }

    if (state == DEVICE_BUSY_NONE) {
        if (existingIdx >= 0) {
            for (int i = existingIdx; i < g_busyCount - 1; i++) {
                g_busyDevices[i] = g_busyDevices[i + 1];
            }
            g_busyCount--;
        }
    } else {
        if (existingIdx >= 0) {
            g_busyDevices[existingIdx].state = state;
        } else if (g_busyCount < MAX_BT_DEVICES) {
            wcsncpy_s(g_busyDevices[g_busyCount].address, 24, address, _TRUNCATE);
            g_busyDevices[g_busyCount].state = state;
            g_busyCount++;
        }
    }

    if (g_hMenuWnd && IsWindowVisible(g_hMenuWnd)) {
        InvalidateRect(g_hMenuWnd, NULL, FALSE);
    }
}

void ui_menu_clear_all_busy(void) {
    g_busyCount = 0;
    if (g_hMenuWnd && IsWindowVisible(g_hMenuWnd)) {
        InvalidateRect(g_hMenuWnd, NULL, FALSE);
    }
}

