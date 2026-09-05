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
#define WM_APP_ACTION_START   (WM_APP + 3)
#define WM_APP_ACTION_DONE    (WM_APP + 4)
#define WM_APP_QUEUE_EMPTY    (WM_APP + 5)

#define TIMER_BUSY_BLINK 1001
#define MAX_QUEUE_SIZE 32

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

typedef struct {
    wchar_t address[24];
    wchar_t name[248];
    bool isConnect;
} QueuedAction;

typedef struct {
    QueuedAction items[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    CRITICAL_SECTION cs;
    HANDLE hWorkEvent;
    HANDLE hStopEvent;
    HANDLE hThread;
    QueuedAction currentItem;
    bool hasCurrent;
} ActionQueue;

typedef struct {
    wchar_t address[24];
    wchar_t name[248];
    bool isConnect;
} ActionStartInfo;

AppState g_appState;
static HWND g_hHiddenWnd = NULL;
static NOTIFYICONDATAW g_nid = { 0 };
static HICON g_hIconDefault = NULL;
static HICON g_hIconConnecting = NULL;
static bool g_blinkState = false;

static ActionQueue g_queue;
static BatchToggleResult g_batchResults = { { 0 }, 0 };
static bool g_batchHadConnect = false;
static bool g_batchHadDisconnect = false;

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
        // If a new action has been queued, abort settled poll early
        EnterCriticalSection(&g_queue.cs);
        bool busyAgain = (g_queue.hasCurrent || g_queue.count > 0);
        LeaveCriticalSection(&g_queue.cs);
        if (busyAgain) break;

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

static DWORD WINAPI queue_worker_thread(LPVOID lpParam) {
    (void)lpParam;
    HANDLE waitHandles[2] = { g_queue.hStopEvent, g_queue.hWorkEvent };

    while (1) {
        DWORD wr = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0) {
            // Stop requested
            break;
        }
        if (wr == WAIT_OBJECT_0 + 1) {
            // Work event signaled: drain items sequentially
            while (1) {
                QueuedAction item = { { 0 } };
                bool hasWork = false;

                EnterCriticalSection(&g_queue.cs);
                if (g_queue.count > 0) {
                    item = g_queue.items[g_queue.head];
                    g_queue.head = (g_queue.head + 1) % MAX_QUEUE_SIZE;
                    g_queue.count--;
                    g_queue.currentItem = item;
                    g_queue.hasCurrent = true;
                    hasWork = true;
                } else {
                    g_queue.hasCurrent = false;
                }
                LeaveCriticalSection(&g_queue.cs);

                if (!hasWork) {
                    PostMessageW(g_hHiddenWnd, WM_APP_QUEUE_EMPTY, 0, 0);
                    break;
                }

                // Notify main thread that this action is starting
                ActionStartInfo* startInfo = (ActionStartInfo*)malloc(sizeof(ActionStartInfo));
                if (startInfo) {
                    wcsncpy_s(startInfo->address, 24, item.address, _TRUNCATE);
                    wcsncpy_s(startInfo->name, 248, item.name, _TRUNCATE);
                    startInfo->isConnect = item.isConnect;
                    PostMessageW(g_hHiddenWnd, WM_APP_ACTION_START, 0, (LPARAM)startInfo);
                }

                // Execute action synchronously
                DeviceToggleResult* tr = (DeviceToggleResult*)malloc(sizeof(DeviceToggleResult));
                if (tr) {
                    memset(tr, 0, sizeof(DeviceToggleResult));
                    if (item.isConnect) {
                        if (g_appState.connectMethod == CONNECT_METHOD_KS) {
                            bt_connect_device_ks(item.address, item.name, tr);
                        } else {
                            bt_connect_device_api(item.address, item.name, tr);
                        }
                    } else {
                        if (g_appState.useHciDisconnect) {
                            bt_disconnect_device_hci(item.address, item.name, tr);
                        } else {
                            bt_disconnect_device_api(item.address, item.name, tr);
                        }
                    }
                    PostMessageW(g_hHiddenWnd, WM_APP_ACTION_DONE, 0, (LPARAM)tr);
                }

                if (WaitForSingleObject(g_queue.hStopEvent, 0) == WAIT_OBJECT_0) {
                    return 0;
                }
            }
        }
    }
    return 0;
}

static void queue_init(void) {
    memset(&g_queue, 0, sizeof(ActionQueue));
    InitializeCriticalSection(&g_queue.cs);
    g_queue.hWorkEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_queue.hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_queue.hThread = CreateThread(NULL, 0, queue_worker_thread, NULL, 0, NULL);
}

static void queue_cleanup(void) {
    if (g_queue.hStopEvent) {
        SetEvent(g_queue.hStopEvent);
    }
    if (g_queue.hThread) {
        WaitForSingleObject(g_queue.hThread, 3000);
        CloseHandle(g_queue.hThread);
        g_queue.hThread = NULL;
    }
    if (g_queue.hWorkEvent) {
        CloseHandle(g_queue.hWorkEvent);
        g_queue.hWorkEvent = NULL;
    }
    if (g_queue.hStopEvent) {
        CloseHandle(g_queue.hStopEvent);
        g_queue.hStopEvent = NULL;
    }
    DeleteCriticalSection(&g_queue.cs);
}

static void on_device_toggle(const wchar_t* address, const wchar_t* name, bool connect) {
    EnterCriticalSection(&g_queue.cs);
    bool alreadyActive = (g_queue.hasCurrent && _wcsicmp(g_queue.currentItem.address, address) == 0);
    bool alreadyQueued = false;
    for (int i = 0; i < g_queue.count; i++) {
        int idx = (g_queue.head + i) % MAX_QUEUE_SIZE;
        if (_wcsicmp(g_queue.items[idx].address, address) == 0) {
            alreadyQueued = true;
            break;
        }
    }
    bool isBusyNow = (g_queue.hasCurrent || g_queue.count > 0);

    if (alreadyActive || alreadyQueued) {
        LeaveCriticalSection(&g_queue.cs);
        return;
    }

    if (g_queue.count < MAX_QUEUE_SIZE) {
        QueuedAction* item = &g_queue.items[g_queue.tail];
        wcsncpy_s(item->address, 24, address, _TRUNCATE);
        wcsncpy_s(item->name, 248, name, _TRUNCATE);
        item->isConnect = connect;
        g_queue.tail = (g_queue.tail + 1) % MAX_QUEUE_SIZE;
        g_queue.count++;
        SetEvent(g_queue.hWorkEvent);
    }
    LeaveCriticalSection(&g_queue.cs);

    ui_menu_set_device_busy(address, isBusyNow ? DEVICE_BUSY_QUEUED : (connect ? DEVICE_BUSY_CONNECTING : DEVICE_BUSY_DISCONNECTING));
    set_busy_state(true);
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
        show_tray_notification(L"uBTAudioTray", L"No devices selected to Connect/Disconnect in the right-click menu.");
        return;
    }

    BluetoothAudioDevice selected[MAX_BT_DEVICES];
    int selCount = 0;
    for (int i = 0; i < g_cachedDeviceCount; i++) {
        if (app_state_is_selected(&g_appState, g_cachedDevices[i].address) && selCount < MAX_BT_DEVICES) {
            selected[selCount++] = g_cachedDevices[i];
        }
    }

    if (selCount == 0) {
        POINT dummyPt = { 0, 0 };
        queue_device_scan(false, dummyPt);
        return;
    }

    bool allConnected = true;
    for (int i = 0; i < selCount; i++) {
        if (!selected[i].isConnected) { allConnected = false; break; }
    }
    bool targetConnect = !allConnected;

    show_tray_notification(L"uBTAudioTray", targetConnect ? L"Connecting selected devices..." : L"Disconnecting selected devices...");

    for (int i = 0; i < selCount; i++) {
        on_device_toggle(selected[i].address, selected[i].name, targetConnect);
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

        case WM_APP_ACTION_START: {
            ActionStartInfo* info = (ActionStartInfo*)lParam;
            if (info) {
                ui_menu_set_device_busy(info->address, info->isConnect ? DEVICE_BUSY_CONNECTING : DEVICE_BUSY_DISCONNECTING);

                wchar_t notifyTitle[64];
                const wchar_t* connMethodStr = L"KS";
                if (g_appState.connectMethod == CONNECT_METHOD_API) connMethodStr = L"API";
                else if (g_appState.connectMethod == CONNECT_METHOD_UI) connMethodStr = L"UI";

                swprintf_s(notifyTitle, 64, L"%s (%s)", info->isConnect ? L"Connecting" : L"Disconnecting",
                    info->isConnect ? connMethodStr
                                    : (g_appState.useHciDisconnect ? L"HCI" : (g_appState.useUiaDisconnect ? L"UI" : L"API")));
                show_tray_notification(notifyTitle, info->name);

                free(info);
            }
            return 0;
        }

        case WM_APP_ACTION_DONE: {
            DeviceToggleResult* tr = (DeviceToggleResult*)lParam;
            if (tr) {
                ui_menu_set_device_busy(tr->deviceAddress, DEVICE_BUSY_NONE);

                if (tr->outcome == TOGGLE_CONNECTED) g_batchHadConnect = true;
                if (tr->outcome == TOGGLE_DISCONNECTED) g_batchHadDisconnect = true;
                if (tr->outcome == TOGGLE_FAILED) {
                    show_tray_notification(L"Action Failed", tr->message);
                }

                if (g_batchResults.count < MAX_BT_DEVICES) {
                    g_batchResults.results[g_batchResults.count++] = *tr;
                }
                free(tr);
            }
            return 0;
        }

        case WM_APP_QUEUE_EMPTY: {
            set_busy_state(false);
            ui_menu_clear_all_busy();

            // Media control
            if (g_batchHadConnect && g_appState.sendMediaPlayOnConnect) {
                Sleep(500);
                media_send_toggle();
            } else if (g_batchHadDisconnect && g_appState.sendMediaPauseOnDisconnect) {
                media_send_toggle();
            }
            g_batchHadConnect = false;
            g_batchHadDisconnect = false;

            // Poll for settled state in background thread
            if (g_batchResults.count > 0) {
                BatchToggleResult* resCopy = (BatchToggleResult*)malloc(sizeof(BatchToggleResult));
                if (resCopy) {
                    *resCopy = g_batchResults;
                    QueueUserWorkItem(worker_poll_settled, resCopy, WT_EXECUTEDEFAULT);
                }
                g_batchResults.count = 0;
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
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"uBTAudioTray_SingleInstance_Mutex");
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
    wc.lpszClassName = L"uBTAudioTray_HiddenMsgWnd";
    RegisterClassExW(&wc);

    g_hHiddenWnd = CreateWindowExW(0, wc.lpszClassName, L"uBTAudioTrayMsg", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    queue_init();

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
    wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"uBTAudioTray");
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
    queue_cleanup();
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

