#include "config.h"
#if ENABLE_UI
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <uiautomationclient.h>
#include <dwmapi.h>
#include "uia_connect.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdbool.h>

static const GUID CLSID_CUIAutomation_Local = { 0xff48dba4, 0x60ef, 0x4201, { 0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e } };
static const GUID IID_IUIAutomation_Local   = { 0x30cbe57d, 0xd9d0, 0x452a, { 0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee } };

static const wchar_t* s_connectNames[] = {
    L"Connect", L"Verbinden", L"Connecter", L"Conectar", L"Connetti"
};
static const wchar_t* s_disconnectNames[] = {
    L"Disconnect", L"Trennen", L"Déconnecter", L"Desconectar", L"Disconnetti"
};

static HWND s_hSettingsSession = NULL;
static bool s_settingsOpenedByApp = false;

static bool is_process_system_settings(DWORD pid) {
    if (pid == 0) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    wchar_t path[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    bool isSettings = false;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        if (wcsstr(path, L"SystemSettings.exe") != NULL) {
            isSettings = true;
        }
    }
    CloseHandle(hProc);
    return isSettings;
}

static bool is_settings_window(HWND hWnd) {
    if (!IsWindow(hWnd) || !IsWindowVisible(hWnd)) return false;

    // Filter out cloaked/suspended UWP windows
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return false;
    }

    wchar_t clsName[128] = { 0 };
    GetClassNameW(hWnd, clsName, 128);
    if (_wcsicmp(clsName, L"ApplicationFrameWindow") != 0 &&
        _wcsicmp(clsName, L"WinUIDesktopWin32WindowClass") != 0) {
        return false;
    }

    // Direct check: process of top-level window
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid != 0 && is_process_system_settings(pid)) {
        return true;
    }

    // For ApplicationFrameWindow, check child CoreWindow owned by SystemSettings.exe
    HWND hChild = FindWindowExW(hWnd, NULL, L"Windows.UI.Core.CoreWindow", NULL);
    if (hChild) {
        DWORD childPid = 0;
        GetWindowThreadProcessId(hChild, &childPid);
        if (childPid != 0 && is_process_system_settings(childPid)) {
            return true;
        }
    }

    // Secondary fallback: window title matching for non-empty titles
    wchar_t title[256] = { 0 };
    GetWindowTextW(hWnd, title, 256);
    if (title[0] != L'\0') {
        if (wcsstr(title, L"Settings") || wcsstr(title, L"Einstellungen") ||
            wcsstr(title, L"Paramètres") || wcsstr(title, L"Configuración") ||
            wcsstr(title, L"Bluetooth") || wcsstr(title, L"Geräte") ||
            wcsstr(title, L"Devices")) {
            return true;
        }
    }

    return false;
}

typedef struct {
    HWND hWnd;
} FindSettingsData;

static BOOL CALLBACK enum_settings_wnd_proc(HWND hWnd, LPARAM lParam) {
    FindSettingsData* data = (FindSettingsData*)lParam;
    if (is_settings_window(hWnd)) {
        data->hWnd = hWnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_settings_hwnd(void) {
    FindSettingsData data = { NULL };
    EnumWindows(enum_settings_wnd_proc, (LPARAM)&data);
    return data.hWnd;
}

static HWND wait_for_settings_hwnd(DWORD timeoutMs) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < timeoutMs) {
        HWND h = find_settings_hwnd();
        if (h) return h;
        Sleep(UIA_FIND_WINDOW_POLL_MS);
    }
    return NULL;
}

static IUIAutomationElement* find_element_by_name(IUIAutomation* pAutomation, IUIAutomationElement* pRoot, const wchar_t* name) {
    if (!pAutomation || !pRoot || !name) return NULL;

    VARIANT varName;
    VariantInit(&varName);
    varName.vt = VT_BSTR;
    varName.bstrVal = SysAllocString(name);
    if (!varName.bstrVal) return NULL;

    IUIAutomationCondition* pCond = NULL;
    HRESULT hr = pAutomation->lpVtbl->CreatePropertyConditionEx(
        pAutomation, UIA_NamePropertyId, varName, PropertyConditionFlags_IgnoreCase, &pCond);
    VariantClear(&varName);
    if (FAILED(hr) || !pCond) return NULL;

    IUIAutomationElement* pFound = NULL;
    pRoot->lpVtbl->FindFirst(pRoot, TreeScope_Descendants, pCond, &pFound);
    pCond->lpVtbl->Release(pCond);
    return pFound;
}

static IUIAutomationElement* find_button_named(IUIAutomation* pAutomation, IUIAutomationElement* pRoot, const wchar_t* name) {
    if (!pAutomation || !pRoot || !name) return NULL;

    VARIANT varName;
    VariantInit(&varName);
    varName.vt = VT_BSTR;
    varName.bstrVal = SysAllocString(name);
    if (!varName.bstrVal) return NULL;

    IUIAutomationCondition* pNameCond = NULL;
    HRESULT hr = pAutomation->lpVtbl->CreatePropertyConditionEx(
        pAutomation, UIA_NamePropertyId, varName, PropertyConditionFlags_IgnoreCase, &pNameCond);
    VariantClear(&varName);
    if (FAILED(hr) || !pNameCond) return NULL;

    VARIANT varType;
    VariantInit(&varType);
    varType.vt = VT_I4;
    varType.lVal = UIA_ButtonControlTypeId;

    IUIAutomationCondition* pTypeCond = NULL;
    hr = pAutomation->lpVtbl->CreatePropertyCondition(pAutomation, UIA_ControlTypePropertyId, varType, &pTypeCond);
    if (FAILED(hr) || !pTypeCond) {
        pNameCond->lpVtbl->Release(pNameCond);
        return NULL;
    }

    IUIAutomationCondition* pAndCond = NULL;
    hr = pAutomation->lpVtbl->CreateAndCondition(pAutomation, pNameCond, pTypeCond, &pAndCond);
    pNameCond->lpVtbl->Release(pNameCond);
    pTypeCond->lpVtbl->Release(pTypeCond);
    if (FAILED(hr) || !pAndCond) return NULL;

    IUIAutomationElement* pFound = NULL;
    pRoot->lpVtbl->FindFirst(pRoot, TreeScope_Descendants, pAndCond, &pFound);
    pAndCond->lpVtbl->Release(pAndCond);
    return pFound;
}

static IUIAutomationElement* find_button_by_automation_id(IUIAutomation* pAutomation, IUIAutomationElement* pRoot, const wchar_t* autoId) {
    if (!pAutomation || !pRoot || !autoId) return NULL;

    VARIANT varId;
    VariantInit(&varId);
    varId.vt = VT_BSTR;
    varId.bstrVal = SysAllocString(autoId);
    if (!varId.bstrVal) return NULL;

    IUIAutomationCondition* pCond = NULL;
    HRESULT hr = pAutomation->lpVtbl->CreatePropertyCondition(pAutomation, UIA_AutomationIdPropertyId, varId, &pCond);
    VariantClear(&varId);
    if (FAILED(hr) || !pCond) return NULL;

    IUIAutomationElement* pFound = NULL;
    pRoot->lpVtbl->FindFirst(pRoot, TreeScope_Descendants, pCond, &pFound);
    pCond->lpVtbl->Release(pCond);
    return pFound;
}

static IUIAutomationElement* find_action_button(IUIAutomation* pAutomation, IUIAutomationElement* pRoot, bool isConnect) {
    const wchar_t** names = isConnect ? s_connectNames : s_disconnectNames;
    size_t count = isConnect ? (sizeof(s_connectNames) / sizeof(s_connectNames[0])) : (sizeof(s_disconnectNames) / sizeof(s_disconnectNames[0]));
    for (size_t i = 0; i < count; i++) {
        IUIAutomationElement* btn = find_button_named(pAutomation, pRoot, names[i]);
        if (btn) return btn;
    }

    const wchar_t* autoId = isConnect ? L"ConnectButton" : L"DisconnectButton";
    IUIAutomationElement* btn = find_button_by_automation_id(pAutomation, pRoot, autoId);
    if (btn) return btn;

    return NULL;
}

static bool is_element_clickable(IUIAutomationElement* pEl) {
    if (!pEl) return false;
    BOOL isEnabled = FALSE;
    if (FAILED(pEl->lpVtbl->get_CurrentIsEnabled(pEl, &isEnabled)) || !isEnabled) return false;

    BOOL isOffscreen = TRUE;
    if (SUCCEEDED(pEl->lpVtbl->get_CurrentIsOffscreen(pEl, &isOffscreen)) && !isOffscreen) {
        return true;
    }

    // Fallback: check bounding rectangle has positive dimensions
    RECT rc = { 0 };
    if (SUCCEEDED(pEl->lpVtbl->get_CurrentBoundingRectangle(pEl, &rc))) {
        if ((rc.right - rc.left > 0) && (rc.bottom - rc.top > 0)) {
            return true;
        }
    }

    return false;
}

static void try_scroll_into_view(IUIAutomationElement* pEl) {
    if (!pEl) return;
    IUIAutomationScrollItemPattern* pScroll = NULL;
    if (SUCCEEDED(pEl->lpVtbl->GetCurrentPattern(pEl, UIA_ScrollItemPatternId, (IUnknown**)&pScroll)) && pScroll) {
        pScroll->lpVtbl->ScrollIntoView(pScroll);
        pScroll->lpVtbl->Release(pScroll);
    }
}

static bool try_invoke_element(IUIAutomationElement* pEl) {
    if (!pEl) return false;
    IUIAutomationInvokePattern* pInv = NULL;
    if (SUCCEEDED(pEl->lpVtbl->GetCurrentPattern(pEl, UIA_InvokePatternId, (IUnknown**)&pInv)) && pInv) {
        HRESULT hr = pInv->lpVtbl->Invoke(pInv);
        pInv->lpVtbl->Release(pInv);
        return SUCCEEDED(hr);
    }
    IUIAutomationTogglePattern* pTog = NULL;
    if (SUCCEEDED(pEl->lpVtbl->GetCurrentPattern(pEl, UIA_TogglePatternId, (IUnknown**)&pTog)) && pTog) {
        HRESULT hr = pTog->lpVtbl->Toggle(pTog);
        pTog->lpVtbl->Release(pTog);
        return SUCCEEDED(hr);
    }
    return false;
}

static IUIAutomationElement* search_for_button_ancestors(IUIAutomation* pAutomation, IUIAutomationElement* pDeviceEl, bool isConnect) {
    if (!pAutomation || !pDeviceEl) return NULL;

    IUIAutomationTreeWalker* pWalker = NULL;
    if (FAILED(pAutomation->lpVtbl->get_RawViewWalker(pAutomation, &pWalker)) || !pWalker) return NULL;

    IUIAutomationElement* pCur = pDeviceEl;
    pCur->lpVtbl->AddRef(pCur);

    IUIAutomationElement* foundBtn = NULL;
    for (int i = 0; i < 6; i++) {
        foundBtn = find_action_button(pAutomation, pCur, isConnect);
        if (foundBtn) break;

        // Stop if current element is ListItem or Group: do not walk into parent List of all devices
        CONTROLTYPEID ctrlType = 0;
        if (SUCCEEDED(pCur->lpVtbl->get_CurrentControlType(pCur, &ctrlType))) {
            if (ctrlType == UIA_ListItemControlTypeId || ctrlType == UIA_GroupControlTypeId) {
                break;
            }
        }

        IUIAutomationElement* pParent = NULL;
        if (FAILED(pWalker->lpVtbl->GetParentElement(pWalker, pCur, &pParent)) || !pParent) {
            break;
        }
        pCur->lpVtbl->Release(pCur);
        pCur = pParent;
    }

    if (pCur) pCur->lpVtbl->Release(pCur);
    pWalker->lpVtbl->Release(pWalker);
    return foundBtn;
}

static void try_scroll_to_top(HWND hWnd) {
    if (hWnd) {
        SetForegroundWindow(hWnd);
        keybd_event(VK_HOME, 0, 0, 0);
        keybd_event(VK_HOME, 0, KEYEVENTF_KEYUP, 0);
    }
}

static void try_scroll_page_down(HWND hWnd) {
    if (hWnd) {
        SetForegroundWindow(hWnd);
        keybd_event(VK_NEXT, 0, 0, 0);
        keybd_event(VK_NEXT, 0, KEYEVENTF_KEYUP, 0);
    }
}

static bool wait_for_bluetooth_page_landmark(IUIAutomation* pAutomation, IUIAutomationElement* pWinEl, DWORD timeoutMs) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < timeoutMs) {
        IUIAutomationElement* btn = find_button_named(pAutomation, pWinEl, L"Add device");
        if (!btn) btn = find_button_named(pAutomation, pWinEl, L"Gerät hinzufügen");
        if (!btn) btn = find_button_named(pAutomation, pWinEl, L"Devices");
        if (!btn) btn = find_button_named(pAutomation, pWinEl, L"Geräte");
        if (btn) {
            btn->lpVtbl->Release(btn);
            return true;
        }

        IUIAutomationElement* devText = find_element_by_name(pAutomation, pWinEl, L"Bluetooth");
        if (devText) {
            devText->lpVtbl->Release(devText);
            return true;
        }

        Sleep(100);
    }
    return false;
}

void uia_close_settings_if_opened(void) {
    if (s_settingsOpenedByApp) {
        HWND h = find_settings_hwnd();
        if (h) {
            PostMessageW(h, WM_CLOSE, 0, 0);
            ULONGLONG start = GetTickCount64();
            while (IsWindow(h) && (GetTickCount64() - start < 500)) {
                Sleep(50);
            }
        }
        s_settingsOpenedByApp = false;
        s_hSettingsSession = NULL;
    }
}

static bool uia_invoke_device_action(const wchar_t* deviceName, const wchar_t* deviceAddress, bool isConnect, bool keepOpen, DeviceToggleResult* result) {
    const wchar_t* actionName = isConnect ? L"Connect" : L"Disconnect";
    if (result) {
        wcscpy_s(result->deviceName, 248, deviceName ? deviceName : L"");
        wcscpy_s(result->deviceAddress, 24, deviceAddress ? deviceAddress : L"");
        result->outcome = TOGGLE_FAILED;
        wcscpy_s(result->message, 256, L"Settings UI action failed.");
    }

    HWND existingWnd = find_settings_hwnd();
    bool hadSettingsWindow = (existingWnd != NULL);

    // If Settings wasn't already open before this session, mark it as opened by us
    if (!hadSettingsWindow && !s_settingsOpenedByApp) {
        s_settingsOpenedByApp = true;
    }

    // Always navigate to Bluetooth Settings page
    ShellExecuteW(NULL, L"open", L"ms-settings:bluetooth", NULL, NULL, SW_SHOWNORMAL);

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool mustUninit = (hrCo == S_OK || hrCo == S_FALSE);

    IUIAutomation* pAutomation = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_CUIAutomation_Local, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation_Local, (void**)&pAutomation);
    if (FAILED(hr) || !pAutomation) {
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Failed to initialize UI Automation.");
        return false;
    }

    HWND hSettings = wait_for_settings_hwnd(UIA_SETTINGS_WINDOW_TIMEOUT_MS);
    if (!hSettings) {
        pAutomation->lpVtbl->Release(pAutomation);
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Timed out waiting for Settings window.");
        return false;
    }

    s_hSettingsSession = hSettings;
    ShowWindow(hSettings, SW_RESTORE);
    SetForegroundWindow(hSettings);

    IUIAutomationElement* pWinEl = NULL;
    hr = pAutomation->lpVtbl->ElementFromHandle(pAutomation, (UIA_HWND)hSettings, &pWinEl);
    if (FAILED(hr) || !pWinEl) {
        pAutomation->lpVtbl->Release(pAutomation);
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Failed to bind Settings window element.");
        return false;
    }

    // Wait for the Bluetooth page landmark to render
    wait_for_bluetooth_page_landmark(pAutomation, pWinEl, UIA_LANDMARK_TIMEOUT_MS);

    // Ensure the device list is scrolled to the top so top devices are materialized
    try_scroll_to_top(hSettings);
    Sleep(UIA_INITIAL_SETTLE_DELAY_MS);

    // Poll for the clickable action button
    IUIAutomationElement* pActionBtn = NULL;
    bool scrolledDownOnce = false;
    ULONGLONG pollStart = GetTickCount64();
    while (GetTickCount64() - pollStart < UIA_BUTTON_READY_TIMEOUT_MS) {
        IUIAutomationElement* pDevEl = find_element_by_name(pAutomation, pWinEl, deviceName);
        if (pDevEl) {
            try_scroll_into_view(pDevEl);
            IUIAutomationElement* btn = search_for_button_ancestors(pAutomation, pDevEl, isConnect);
            pDevEl->lpVtbl->Release(pDevEl);

            if (btn) {
                if (is_element_clickable(btn)) {
                    pActionBtn = btn;
                    break;
                }
                btn->lpVtbl->Release(btn);
            }
        } else {
            // If device not visible at the top after initial settle, scroll down once
            if (!scrolledDownOnce && (GetTickCount64() - pollStart > UIA_SCROLL_PAGEDOWN_DELAY_MS)) {
                try_scroll_page_down(hSettings);
                scrolledDownOnce = true;
            }
        }
        Sleep(UIA_READY_POLL_INTERVAL_MS);
    }

    if (!pActionBtn) {
        pWinEl->lpVtbl->Release(pWinEl);
        pAutomation->lpVtbl->Release(pAutomation);
        if (!keepOpen) {
            uia_close_settings_if_opened();
        }
        if (mustUninit) CoUninitialize();
        if (result) swprintf_s(result->message, 256, L"Button '%s' not found for '%s'.", actionName, deviceName);
        return false;
    }

    // Invoke action button
    bool invoked = try_invoke_element(pActionBtn);
    pActionBtn->lpVtbl->Release(pActionBtn);

    if (!invoked) {
        pWinEl->lpVtbl->Release(pWinEl);
        pAutomation->lpVtbl->Release(pAutomation);
        if (!keepOpen) {
            uia_close_settings_if_opened();
        }
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Failed to invoke action button.");
        return false;
    }

    // Wait for state transition confirmation
    bool confirmed = false;
    ULONGLONG confirmStart = GetTickCount64();
    while (GetTickCount64() - confirmStart < UIA_POST_CLICK_CONFIRM_TIMEOUT_MS) {
        IUIAutomationElement* pDevEl = find_element_by_name(pAutomation, pWinEl, deviceName);
        if (pDevEl) {
            IUIAutomationElement* nextBtn = search_for_button_ancestors(pAutomation, pDevEl, !isConnect);
            if (nextBtn) {
                nextBtn->lpVtbl->Release(nextBtn);
                pDevEl->lpVtbl->Release(pDevEl);
                confirmed = true;
                break;
            }

            IUIAutomationElement* prevBtn = search_for_button_ancestors(pAutomation, pDevEl, isConnect);
            if (!prevBtn || !is_element_clickable(prevBtn)) {
                if (prevBtn) prevBtn->lpVtbl->Release(prevBtn);
                pDevEl->lpVtbl->Release(pDevEl);
                confirmed = true;
                break;
            }
            if (prevBtn) prevBtn->lpVtbl->Release(prevBtn);
            pDevEl->lpVtbl->Release(pDevEl);
        }
        Sleep(UIA_POST_CLICK_CONFIRM_POLL_MS);
    }

    pWinEl->lpVtbl->Release(pWinEl);
    pAutomation->lpVtbl->Release(pAutomation);

    // Auto-close Settings window if we opened it and no more actions are queued
    if (!keepOpen) {
        Sleep(confirmed ? UIA_CLOSE_DELAY_CONFIRMED_MS : UIA_CLOSE_DELAY_UNCONFIRMED_MS);
        uia_close_settings_if_opened();
    }

    if (mustUninit) CoUninitialize();

    if (result) {
        result->outcome = isConnect ? TOGGLE_CONNECTED : TOGGLE_DISCONNECTED;
        swprintf_s(result->message, 256, L"%s via Settings UI.", isConnect ? L"Connected" : L"Disconnected");
    }
    return true;
}

bool uia_connect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, bool keepOpen, DeviceToggleResult* result) {
    return uia_invoke_device_action(deviceName, deviceAddress, true, keepOpen, result);
}

bool uia_disconnect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, bool keepOpen, DeviceToggleResult* result) {
    return uia_invoke_device_action(deviceName, deviceAddress, false, keepOpen, result);
}

#endif // ENABLE_UI
