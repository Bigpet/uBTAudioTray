#include "audio_state_notification.h"
#include <mmdeviceapi.h>
#include <string.h>

static const GUID CLSID_MMDeviceEnumerator_Notify = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID IID_IMMDeviceEnumerator_Notify  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID IID_IUnknown_Notify             = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IMMNotificationClient_Notify = { 0x7991EEC9, 0x7E89, 0x4D85, { 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0 } };

static HWND g_notifyHwnd = NULL;
static UINT g_notifyMsg = 0;
static IMMDeviceEnumerator* g_pNotifyEnumerator = NULL;

static bool guid_matches(const GUID* a, const GUID* b) {
    return (memcmp(a, b, sizeof(GUID)) == 0);
}

static HRESULT STDMETHODCALLTYPE notify_QueryInterface(IMMNotificationClient* This, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (guid_matches(riid, &IID_IUnknown_Notify) || guid_matches(riid, &IID_IMMNotificationClient_Notify)) {
        *ppvObject = This;
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE notify_AddRef(IMMNotificationClient* This) {
    (void)This;
    return 1;
}

static ULONG STDMETHODCALLTYPE notify_Release(IMMNotificationClient* This) {
    (void)This;
    return 1;
}

static HRESULT STDMETHODCALLTYPE notify_OnDeviceStateChanged(IMMNotificationClient* This, LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    (void)This; (void)pwstrDeviceId; (void)dwNewState;
    if (g_notifyHwnd && g_notifyMsg) {
        PostMessageW(g_notifyHwnd, g_notifyMsg, 0, 0);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE notify_OnDeviceAdded(IMMNotificationClient* This, LPCWSTR pwstrDeviceId) {
    (void)This; (void)pwstrDeviceId;
    if (g_notifyHwnd && g_notifyMsg) {
        PostMessageW(g_notifyHwnd, g_notifyMsg, 0, 0);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE notify_OnDeviceRemoved(IMMNotificationClient* This, LPCWSTR pwstrDeviceId) {
    (void)This; (void)pwstrDeviceId;
    if (g_notifyHwnd && g_notifyMsg) {
        PostMessageW(g_notifyHwnd, g_notifyMsg, 0, 0);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE notify_OnDefaultDeviceChanged(IMMNotificationClient* This, EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
    (void)This; (void)flow; (void)role; (void)pwstrDefaultDeviceId;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE notify_OnPropertyValueChanged(IMMNotificationClient* This, LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
    (void)This; (void)pwstrDeviceId; (void)key;
    return S_OK;
}

static IMMNotificationClientVtbl g_notifyClientVtbl = {
    notify_QueryInterface,
    notify_AddRef,
    notify_Release,
    notify_OnDeviceStateChanged,
    notify_OnDeviceAdded,
    notify_OnDeviceRemoved,
    notify_OnDefaultDeviceChanged,
    notify_OnPropertyValueChanged
};

static IMMNotificationClient g_audioNotifyClient = {
    &g_notifyClientVtbl
};

bool audio_state_notification_register(HWND hWnd, UINT msg) {
    if (g_pNotifyEnumerator) return true;

    g_notifyHwnd = hWnd;
    g_notifyMsg = msg;

    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_Notify, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_Notify, (void**)&g_pNotifyEnumerator);
    if (FAILED(hr) || !g_pNotifyEnumerator) {
        g_pNotifyEnumerator = NULL;
        return false;
    }

    hr = g_pNotifyEnumerator->lpVtbl->RegisterEndpointNotificationCallback(g_pNotifyEnumerator, &g_audioNotifyClient);
    if (FAILED(hr)) {
        g_pNotifyEnumerator->lpVtbl->Release(g_pNotifyEnumerator);
        g_pNotifyEnumerator = NULL;
        return false;
    }
    return true;
}

void audio_state_notification_unregister(void) {
    if (g_pNotifyEnumerator) {
        g_pNotifyEnumerator->lpVtbl->UnregisterEndpointNotificationCallback(g_pNotifyEnumerator, &g_audioNotifyClient);
        g_pNotifyEnumerator->lpVtbl->Release(g_pNotifyEnumerator);
        g_pNotifyEnumerator = NULL;
    }
    g_notifyHwnd = NULL;
    g_notifyMsg = 0;
}
