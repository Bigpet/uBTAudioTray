#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "resource.h"
#include "app_state.h"
#include "startup.h"
#include "theme.h"
#include "media.h"
#include "bluetooth.h"
#include "ui_common.h"
#include "ui_menu.h"
#include "ui_settings.h"

#define WM_APP_TRAYMSG        (WM_APP + 1)
#define WM_APP_SCAN_DONE      (WM_APP + 2)
#define WM_APP_TOGGLE_DONE    (WM_APP + 3)

#define TIMER_BUSY_BLINK 1001

typedef struct {
    BluetoothAudioDevice devices[MAX_BT_DEVICES];
    int count;
    bool showMenuAfterScan;
    POINT anchorPt;
} ScanResult;

typedef struct {
    DeviceToggleResult results[MAX_BT_DEVICES];
    int count;
} BatchToggleResult;

AppState g_appState;
static HWND g_hHiddenWnd = NULL;
static NOTIFYICONDATAW g_nid = { 0 };
static HICON g_hIconDefault = NULL;
static HICON g_hIconConnecting = NULL;
static bool g_blinkState = false;
static volatile LONG g_isOperationBusy = 0;

static BluetoothAudioDevice g_cachedDevices[MAX_BT_DEVICES];
static int g_cachedDeviceCount = 0;

static void show_tray_notification(const wchar_t* title, const wchar_t* message) {
    if (!g_appState.enableNotifications) return;

    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 3000;
    wcscpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle) / sizeof(wchar_t), title);
    wcscpy_s(nid.szInfo, sizeof(nid.szInfo) / sizeof(wchar_t), message);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void set_busy_state(bool isBusy) {
    if (isBusy) {
        ui_menu_set_busy(true);
        g_blinkState = true;
        g_nid.hIcon = g_hIconConnecting;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        SetTimer(g_hHiddenWnd, TIMER_BUSY_BLINK, 250, NULL);
    } else {
        KillTimer(g_hHiddenWnd, TIMER_BUSY_BLINK);
        g_blinkState = false;
        g_nid.hIcon = g_hIconDefault;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        ui_menu_set_busy(false);
    }
}

// Background worker for device discovery
static DWORD WINAPI worker_scan_devices(LPVOID lpParam) {
    ScanResult* req = (ScanResult*)lpParam;
    ScanResult* res = (ScanResult*)malloc(sizeof(ScanResult));
    if (!res) return 0;
    memset(res, 0, sizeof(ScanResult));

    if (req) {
        res->showMenuAfterScan = req->showMenuAfterScan;
        res->anchorPt = req->anchorPt;
        free(req);
    }

    res->count = bt_discover_audio_devices(res->devices, MAX_BT_DEVICES);
    PostMessageW(g_hHiddenWnd, WM_APP_SCAN_DONE, 0, (LPARAM)res);
    return 0;
}

static void queue_device_scan(bool showMenuAfterScan, POINT anchorPt) {
    ScanResult* req = (ScanResult*)malloc(sizeof(ScanResult));
    if (req) {
        req->showMenuAfterScan = showMenuAfterScan;
        req->anchorPt = anchorPt;
        QueueUserWorkItem(worker_scan_devices, req, WT_EXECUTEDEFAULT);
    }
}

// Background worker for polling settled state after toggle
static DWORD WINAPI worker_poll_settled(LPVOID lpParam) {
    BatchToggleResult* toggleRes = (BatchToggleResult*)lpParam;

    ScanResult* res = (ScanResult*)malloc(sizeof(ScanResult));
    if (!res) {
        if (toggleRes) free(toggleRes);
        return 0;
    }
    memset(res, 0, sizeof(ScanResult));

    bool settled = false;
    for (int attempt = 0; attempt < 8; attempt++) {
        res->count = bt_discover_audio_devices(res->devices, MAX_BT_DEVICES);

        settled = true;
        if (toggleRes) {
            for (int i = 0; i < toggleRes->count; i++) {
                DeviceToggleResult* tr = &toggleRes->results[i];
                bool expectedConnected = (tr->outcome == TOGGLE_CONNECTED);
                for (int j = 0; j < res->count; j++) {
                    if (_wcsicmp(res->devices[j].address, tr->deviceAddress) == 0) {
                        if (res->devices[j].isConnected != expectedConnected) {
                            settled = false;
                        }
                        break;
                    }
                }
                if (!settled) break;
            }
        }

        if (settled) break;
        Sleep(350);
    }

    if (toggleRes) free(toggleRes);
    PostMessageW(g_hHiddenWnd, WM_APP_SCAN_DONE, 0, (LPARAM)res);
    return 0;
}

typedef struct {
    wchar_t address[24];
    wchar_t name[248];
    bool isConnect;
    bool isBatch;
} ToggleWorkerParam;

// Background worker for connecting/disconnecting devices
static DWORD WINAPI worker_toggle_device(LPVOID lpParam) {
    ToggleWorkerParam* param = (ToggleWorkerParam*)lpParam;
    BatchToggleResult* res = (BatchToggleResult*)malloc(sizeof(BatchToggleResult));
    if (!res) {
        free(param);
        InterlockedExchange(&g_isOperationBusy, 0);
        return 0;
    }
    memset(res, 0, sizeof(BatchToggleResult));

    if (param->isBatch) {
        // Batch toggle all selected devices
        BluetoothAudioDevice devices[MAX_BT_DEVICES];
        int count = bt_discover_audio_devices(devices, MAX_BT_DEVICES);

        // Find selected devices
        BluetoothAudioDevice selected[MAX_BT_DEVICES];
        int selCount = 0;
        for (int i = 0; i < count; i++) {
            if (app_state_is_selected(&g_appState, devices[i].address) && selCount < MAX_BT_DEVICES) {
                selected[selCount++] = devices[i];
            }
        }

        if (selCount > 0) {
            bool allConnected = true;
            for (int i = 0; i < selCount; i++) {
                if (!selected[i].isConnected) { allConnected = false; break; }
            }

            for (int i = 0; i < selCount; i++) {
                DeviceToggleResult* r = &res->results[res->count++];
                if (allConnected) {
                    // Disconnect
                    if (g_appState.useHciDisconnect) {
                        bt_disconnect_device_hci(selected[i].address, selected[i].name, r);
                    } else {
                        bt_disconnect_device_api(selected[i].address, selected[i].name, r);
                    }
                } else {
                    // Connect
                    bt_connect_device_api(selected[i].address, selected[i].name, r);
                }
            }
        }
    } else {
        // Single device toggle
        DeviceToggleResult* r = &res->results[res->count++];
        if (param->isConnect) {
            bt_connect_device_api(param->address, param->name, r);
        } else {
            if (g_appState.useHciDisconnect) {
                bt_disconnect_device_hci(param->address, param->name, r);
            } else {
                bt_disconnect_device_api(param->address, param->name, r);
            }
        }
    }

    free(param);
    PostMessageW(g_hHiddenWnd, WM_APP_TOGGLE_DONE, 0, (LPARAM)res);
    return 0;
}

static void on_device_toggle(const wchar_t* address, const wchar_t* name, bool connect) {
    if (InterlockedCompareExchange(&g_isOperationBusy, 1, 0) != 0) {
        show_tray_notification(L"QuickBTTray", L"A Bluetooth action is already running.");
        return;
    }

    set_busy_state(true);

    wchar_t notifyTitle[64];
    swprintf_s(notifyTitle, 64, L"%s (%s)", connect ? L"Connecting" : L"Disconnecting",
        connect ? (g_appState.useUiaConnect ? L"UI" : L"API")
                : (g_appState.useHciDisconnect ? L"HCI" : (g_appState.useUiaDisconnect ? L"UI" : L"API")));
    show_tray_notification(notifyTitle, name);

    ToggleWorkerParam* p = (ToggleWorkerParam*)malloc(sizeof(ToggleWorkerParam));
    if (p) {
        wcscpy_s(p->address, 24, address);
        wcscpy_s(p->name, 248, name);
        p->isConnect = connect;
        p->isBatch = false;
        QueueUserWorkItem(worker_toggle_device, p, WT_EXECUTEDEFAULT);
    }
}

static void on_settings_requested(void) {
    POINT pt;
    GetCursorPos(&pt);
    ui_settings_show(pt.x, pt.y);
}

static void on_exit_requested(void) {
    PostQuitMessage(0);
}

static void on_tray_left_click(void) {
    if (g_appState.selectedCount == 0) {
        show_tray_notification(L"QuickBTTray", L"No devices selected to Connect/Disconnect in the right-click menu.");
        return;
    }

    if (InterlockedCompareExchange(&g_isOperationBusy, 1, 0) != 0) {
        show_tray_notification(L"QuickBTTray", L"A Bluetooth action is already running.");
        return;
    }

    set_busy_state(true);
    show_tray_notification(L"QuickBTTray", L"Toggling selected Bluetooth audio devices...");

    ToggleWorkerParam* p = (ToggleWorkerParam*)malloc(sizeof(ToggleWorkerParam));
    if (p) {
        memset(p, 0, sizeof(ToggleWorkerParam));
        p->isBatch = true;
        QueueUserWorkItem(worker_toggle_device, p, WT_EXECUTEDEFAULT);
    }
}

static void on_tray_right_click(void) {
    POINT pt;
    GetCursorPos(&pt);

    if (ui_menu_is_visible()) {
        ui_menu_hide();
        return;
    }

    if (g_cachedDeviceCount > 0) {
        // Show immediately with cached data, then refresh in background
        ui_menu_show(pt.x, pt.y);
        queue_device_scan(false, pt);
    } else {
        // First scan: wait for discovery before showing
        queue_device_scan(true, pt);
    }
}

static LRESULT CALLBACK hidden_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_APP_TRAYMSG:
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP:
                    on_tray_right_click();
                    break;
                case WM_LBUTTONUP:
                    on_tray_left_click();
                    break;
            }
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_BUSY_BLINK) {
                g_blinkState = !g_blinkState;
                g_nid.hIcon = g_blinkState ? g_hIconConnecting : g_hIconDefault;
                Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            }
            return 0;

        case WM_APP_SCAN_DONE: {
            ScanResult* res = (ScanResult*)lParam;
            if (res) {
                g_cachedDeviceCount = res->count;
                memcpy(g_cachedDevices, res->devices, res->count * sizeof(BluetoothAudioDevice));

                // Prune any selected addresses that no longer exist
                wchar_t activeAddrs[MAX_BT_DEVICES][MAC_ADDR_LEN];
                for (int i = 0; i < res->count; i++) {
                    wcscpy_s(activeAddrs[i], MAC_ADDR_LEN, res->devices[i].address);
                }
                app_state_prune_unseen(&g_appState, activeAddrs, res->count);
                app_state_save(&g_appState);

                ui_menu_update_devices(res->devices, res->count);

                if (res->showMenuAfterScan) {
                    ui_menu_show(res->anchorPt.x, res->anchorPt.y);
                }
                free(res);
            }
            return 0;
        }

        case WM_APP_TOGGLE_DONE: {
            BatchToggleResult* res = (BatchToggleResult*)lParam;
            set_busy_state(false);
            InterlockedExchange(&g_isOperationBusy, 0);

            if (res) {
                bool hasConnect = false;
                bool hasDisconnect = false;
                int failCount = 0;

                for (int i = 0; i < res->count; i++) {
                    if (res->results[i].outcome == TOGGLE_CONNECTED) hasConnect = true;
                    if (res->results[i].outcome == TOGGLE_DISCONNECTED) hasDisconnect = true;
                    if (res->results[i].outcome == TOGGLE_FAILED) {
                        failCount++;
                        show_tray_notification(L"Action Failed", res->results[i].message);
                    }
                }

                // Media control
                if (hasConnect && g_appState.sendMediaPlayOnConnect) {
                    Sleep(500);
                    media_send_toggle();
                }
                if (hasDisconnect && g_appState.sendMediaPauseOnDisconnect) {
                    media_send_toggle();
                }

                // Poll for settled state in background thread
                QueueUserWorkItem(worker_poll_settled, res, WT_EXECUTEDEFAULT);
            }
            return 0;
        }

        case WM_SETTINGCHANGE:
            // Windows Dark/Light mode theme change
            if (ui_menu_is_visible()) InvalidateRect(ui_menu_get_hwnd(), NULL, TRUE);
            if (ui_settings_is_visible()) InvalidateRect(ui_settings_get_hwnd(), NULL, TRUE);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Single instance mutex
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"QuickBTTray_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    app_state_load(&g_appState);

    int smWidth = GetSystemMetrics(SM_CXSMICON);
    int smHeight = GetSystemMetrics(SM_CYSMICON);
    g_hIconDefault = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, smWidth, smHeight, LR_DEFAULTCOLOR);
    g_hIconConnecting = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_CONNECTING_ICON), IMAGE_ICON, smWidth, smHeight, LR_DEFAULTCOLOR);

    // Fallbacks if resources not found
    if (!g_hIconDefault) g_hIconDefault = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!g_hIconConnecting) g_hIconConnecting = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_CONNECTING_ICON));
    if (!g_hIconDefault) g_hIconDefault = LoadIconW(NULL, IDI_APPLICATION);
    if (!g_hIconConnecting) g_hIconConnecting = g_hIconDefault;

    // Register hidden message window class
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = hidden_wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"QuickBTTray_HiddenMsgWnd";
    RegisterClassExW(&wc);

    g_hHiddenWnd = CreateWindowExW(0, wc.lpszClassName, L"QuickBTTrayMsg", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    // Initialize UI windows
    ui_menu_init(hInstance, g_hHiddenWnd);
    ui_settings_init(hInstance, g_hHiddenWnd);
    ui_menu_set_callbacks(on_device_toggle, on_settings_requested, on_exit_requested);

    // Register system tray icon
    memset(&g_nid, 0, sizeof(NOTIFYICONDATAW));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hHiddenWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_APP_TRAYMSG;
    g_nid.hIcon = g_hIconDefault;
    wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"QuickBTTray");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Initial background scan
    POINT dummyPt = { 0, 0 };
    queue_device_scan(false, dummyPt);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ui_menu_cleanup();
    ui_settings_cleanup();
    bt_cleanup();
    CoUninitialize();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}

