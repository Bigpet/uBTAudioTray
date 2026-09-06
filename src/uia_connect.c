#include "config.h"
#if ENABLE_UI
#define COBJMACROS
#include "uia_connect.h"
#include <uiautomationclient.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdbool.h>

static const GUID CLSID_CUIAutomation_Local = { 0xff48dba4, 0x60ef, 0x4201, { 0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e } };
static const GUID IID_IUIAutomation_Local   = { 0x30cbe57d, 0xd9d0, 0x452a, { 0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee } };

#define SETTINGS_WINDOW_TIMEOUT_MS  8000
#define BUTTON_READY_TIMEOUT_MS     6000
#define INITIAL_SETTLE_DELAY_MS      200
#define READY_POLL_INTERVAL_MS       200
#define POST_CLICK_CONFIRM_TIMEOUT   900
#define POST_CLICK_CONFIRM_POLL      150

typedef struct {
    HWND hWnd;
} FindSettingsData;

static BOOL CALLBACK enum_settings_wnd_proc(HWND hWnd, LPARAM lParam) {
    FindSettingsData* data = (FindSettingsData*)lParam;
    if (!IsWindowVisible(hWnd)) return TRUE;

    wchar_t clsName[128] = { 0 };
    GetClassNameW(hWnd, clsName, 128);
    if (_wcsicmp(clsName, L"ApplicationFrameWindow") == 0 ||
        _wcsicmp(clsName, L"WinUIDesktopWin32WindowClass") == 0) {
        wchar_t title[256] = { 0 };
        GetWindowTextW(hWnd, title, 256);
        if (wcsstr(title, L"Settings") != NULL || wcsstr(title, L"Einstellungen") != NULL || title[0] == L'\0') {
            data->hWnd = hWnd;
            return FALSE;
        }
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
        Sleep(250);
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

static bool is_element_clickable(IUIAutomationElement* pEl) {
    if (!pEl) return false;
    BOOL isEnabled = FALSE;
    if (FAILED(pEl->lpVtbl->get_CurrentIsEnabled(pEl, &isEnabled)) || !isEnabled) return false;

    BOOL isOffscreen = TRUE;
    if (FAILED(pEl->lpVtbl->get_CurrentIsOffscreen(pEl, &isOffscreen)) || isOffscreen) return false;

    return true;
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
    return false;
}

static IUIAutomationElement* search_for_button_ancestors(IUIAutomation* pAutomation, IUIAutomationElement* pDeviceEl, const wchar_t* btnName) {
    if (!pAutomation || !pDeviceEl || !btnName) return NULL;

    IUIAutomationTreeWalker* pWalker = NULL;
    if (FAILED(pAutomation->lpVtbl->get_RawViewWalker(pAutomation, &pWalker)) || !pWalker) return NULL;

    IUIAutomationElement* pCur = pDeviceEl;
    pCur->lpVtbl->AddRef(pCur);

    IUIAutomationElement* foundBtn = NULL;
    for (int i = 0; i < 12; i++) {
        foundBtn = find_button_named(pAutomation, pCur, btnName);
        if (foundBtn) break;

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

static void try_send_end_key(HWND hWnd) {
    if (hWnd) {
        SetForegroundWindow(hWnd);
        keybd_event(VK_END, 0, 0, 0);
        keybd_event(VK_END, 0, KEYEVENTF_KEYUP, 0);
    }
}

static bool uia_invoke_device_action(const wchar_t* deviceName, const wchar_t* deviceAddress, const wchar_t* action, DeviceToggleResult* result) {
    if (result) {
        wcscpy_s(result->deviceName, 248, deviceName ? deviceName : L"");
        wcscpy_s(result->deviceAddress, 24, deviceAddress ? deviceAddress : L"");
        result->outcome = TOGGLE_FAILED;
        wcscpy_s(result->message, 256, L"Settings UI action failed.");
    }

    bool hadSettingsWindow = (find_settings_hwnd() != NULL);

    // Launch Settings Bluetooth page
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

    HWND hSettings = wait_for_settings_hwnd(SETTINGS_WINDOW_TIMEOUT_MS);
    if (!hSettings) {
        pAutomation->lpVtbl->Release(pAutomation);
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Timed out waiting for Settings window.");
        return false;
    }

    ShowWindow(hSettings, SW_RESTORE);

    IUIAutomationElement* pWinEl = NULL;
    hr = pAutomation->lpVtbl->ElementFromHandle(pAutomation, (UIA_HWND)hSettings, &pWinEl);
    if (FAILED(hr) || !pWinEl) {
        pAutomation->lpVtbl->Release(pAutomation);
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Failed to bind Settings window element.");
        return false;
    }

    // Wait for landmark ("Add device" or "Devices") or brief settle
    Sleep(INITIAL_SETTLE_DELAY_MS);

    // Nudge WinUI device list virtualization
    try_send_end_key(hSettings);
    Sleep(INITIAL_SETTLE_DELAY_MS);

    // Poll for the clickable button
    IUIAutomationElement* pActionBtn = NULL;
    ULONGLONG pollStart = GetTickCount64();
    while (GetTickCount64() - pollStart < BUTTON_READY_TIMEOUT_MS) {
        IUIAutomationElement* pDevEl = find_element_by_name(pAutomation, pWinEl, deviceName);
        if (pDevEl) {
            try_scroll_into_view(pDevEl);
            IUIAutomationElement* btn = search_for_button_ancestors(pAutomation, pDevEl, action);
            pDevEl->lpVtbl->Release(pDevEl);

            if (btn) {
                if (is_element_clickable(btn)) {
                    pActionBtn = btn;
                    break;
                }
                btn->lpVtbl->Release(btn);
            }
        } else {
            try_send_end_key(hSettings);
        }
        Sleep(READY_POLL_INTERVAL_MS);
    }

    if (!pActionBtn) {
        pWinEl->lpVtbl->Release(pWinEl);
        pAutomation->lpVtbl->Release(pAutomation);
        if (!hadSettingsWindow) PostMessageW(hSettings, WM_CLOSE, 0, 0);
        if (mustUninit) CoUninitialize();
        if (result) swprintf_s(result->message, 256, L"Button '%s' not found for '%s'.", action, deviceName);
        return false;
    }

    // Invoke action button
    bool invoked = try_invoke_element(pActionBtn);
    pActionBtn->lpVtbl->Release(pActionBtn);

    if (!invoked) {
        pWinEl->lpVtbl->Release(pWinEl);
        pAutomation->lpVtbl->Release(pAutomation);
        if (!hadSettingsWindow) PostMessageW(hSettings, WM_CLOSE, 0, 0);
        if (mustUninit) CoUninitialize();
        if (result) wcscpy_s(result->message, 256, L"Failed to invoke action button.");
        return false;
    }

    // Wait for state transition confirmation
    const wchar_t* expectedOpposite = (_wcsicmp(action, L"Connect") == 0) ? L"Disconnect" : L"Connect";
    bool confirmed = false;
    ULONGLONG confirmStart = GetTickCount64();
    while (GetTickCount64() - confirmStart < POST_CLICK_CONFIRM_TIMEOUT) {
        IUIAutomationElement* pDevEl = find_element_by_name(pAutomation, pWinEl, deviceName);
        if (pDevEl) {
            IUIAutomationElement* nextBtn = search_for_button_ancestors(pAutomation, pDevEl, expectedOpposite);
            if (nextBtn) {
                nextBtn->lpVtbl->Release(nextBtn);
                pDevEl->lpVtbl->Release(pDevEl);
                confirmed = true;
                break;
            }

            IUIAutomationElement* prevBtn = search_for_button_ancestors(pAutomation, pDevEl, action);
            if (!prevBtn || !is_element_clickable(prevBtn)) {
                if (prevBtn) prevBtn->lpVtbl->Release(prevBtn);
                pDevEl->lpVtbl->Release(pDevEl);
                confirmed = true;
                break;
            }
            if (prevBtn) prevBtn->lpVtbl->Release(prevBtn);
            pDevEl->lpVtbl->Release(pDevEl);
        }
        Sleep(POST_CLICK_CONFIRM_POLL);
    }

    pWinEl->lpVtbl->Release(pWinEl);
    pAutomation->lpVtbl->Release(pAutomation);

    // Auto-close Settings window if we opened it
    if (!hadSettingsWindow) {
        Sleep(confirmed ? 180 : 120);
        PostMessageW(hSettings, WM_CLOSE, 0, 0);
    }

    if (mustUninit) CoUninitialize();

    bool isConnect = (_wcsicmp(action, L"Connect") == 0);
    if (result) {
        result->outcome = isConnect ? TOGGLE_CONNECTED : TOGGLE_DISCONNECTED;
        swprintf_s(result->message, 256, L"%s via Settings UI.", isConnect ? L"Connected" : L"Disconnected");
    }
    return true;
}

bool uia_connect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, DeviceToggleResult* result) {
    return uia_invoke_device_action(deviceName, deviceAddress, L"Connect", result);
}

bool uia_disconnect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, DeviceToggleResult* result) {
    return uia_invoke_device_action(deviceName, deviceAddress, L"Disconnect", result);
}
#endif // ENABLE_UI

