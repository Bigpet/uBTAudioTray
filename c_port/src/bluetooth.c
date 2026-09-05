#include "bluetooth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <functiondiscoverykeys_devpkey.h>

#define IOCTL_BTH_DISCONNECT_DEVICE 0x41000c
#define CACHE_TTL_MS 45000
#define MAX_CACHED_SERVICES 16
#define MAX_CACHE_ENTRIES 32

static const GUID GUID_Handsfree = { 0x0000111e, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
static const GUID GUID_AudioSink = { 0x0000110b, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };

static const GUID CLSID_MMDeviceEnumerator_Bt = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID IID_IMMDeviceEnumerator_Bt  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID IID_IDeviceTopology_Bt      = { 0x2A07407E, 0x6497, 0x4A18, { 0x97, 0x87, 0x32, 0xF7, 0x9B, 0xD0, 0xD9, 0x8F } };
static const GUID IID_IPart_Bt                = { 0xAE2DE0E4, 0x5BCA, 0x4F2D, { 0xAA, 0x46, 0x5D, 0x13, 0xF8, 0xFD, 0xB3, 0xA9 } };
static const GUID IID_IKsControl_Bt           = { 0x28F54685, 0x06FD, 0x11D2, { 0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
static const GUID KSPROPSETID_BtAudio_Bt      = { 0x7FA06C40, 0xB8F6, 0x4C7E, { 0x85, 0x56, 0xE8, 0xC3, 0x3A, 0x12, 0xE5, 0x4D } };

static const PROPERTYKEY PKEY_Device_FriendlyName_Bt = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};

#ifndef KSPROPERTY_ONESHOT_RECONNECT
#define KSPROPERTY_ONESHOT_RECONNECT 0
#endif
#ifndef KSPROPERTY_ONESHOT_DISCONNECT
#define KSPROPERTY_ONESHOT_DISCONNECT 1
#endif

typedef struct {
    wchar_t address[24];
    ULONGLONG expiresAt;
    GUID services[MAX_CACHED_SERVICES];
    DWORD serviceCount;
    bool isValid;
} CachedServices;

static CachedServices g_serviceCache[MAX_CACHE_ENTRIES];
static CRITICAL_SECTION g_cacheLock;
static bool g_initialized = false;

void bt_init(void) {
    if (!g_initialized) {
        InitializeCriticalSection(&g_cacheLock);
        memset(g_serviceCache, 0, sizeof(g_serviceCache));
        g_initialized = true;
    }
}

void bt_cleanup(void) {
    if (g_initialized) {
        DeleteCriticalSection(&g_cacheLock);
        g_initialized = false;
    }
}

void bt_format_address(ULONGLONG addr, wchar_t* outStr, size_t maxLen) {
    swprintf_s(outStr, maxLen, L"%02X:%02X:%02X:%02X:%02X:%02X",
        (unsigned int)((addr >> 40) & 0xFF),
        (unsigned int)((addr >> 32) & 0xFF),
        (unsigned int)((addr >> 24) & 0xFF),
        (unsigned int)((addr >> 16) & 0xFF),
        (unsigned int)((addr >> 8) & 0xFF),
        (unsigned int)(addr & 0xFF));
}

ULONGLONG bt_parse_address(const wchar_t* address) {
    unsigned int b[6] = { 0 };
    if (swscanf_s(address, L"%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        return (((ULONGLONG)b[0] & 0xFF) << 40) |
               (((ULONGLONG)b[1] & 0xFF) << 32) |
               (((ULONGLONG)b[2] & 0xFF) << 24) |
               (((ULONGLONG)b[3] & 0xFF) << 16) |
               (((ULONGLONG)b[4] & 0xFF) << 8)  |
               (((ULONGLONG)b[5] & 0xFF));
    }
    return 0;
}

static void invalidate_service_cache(const wchar_t* address) {
    EnterCriticalSection(&g_cacheLock);
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (g_serviceCache[i].isValid && _wcsicmp(g_serviceCache[i].address, address) == 0) {
            g_serviceCache[i].isValid = false;
            break;
        }
    }
    LeaveCriticalSection(&g_cacheLock);
}

static DWORD get_installed_services_cached(BLUETOOTH_DEVICE_INFO* info, const wchar_t* address, GUID* outGuids, DWORD maxGuids) {
    ULONGLONG now = GetTickCount64();
    EnterCriticalSection(&g_cacheLock);

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (g_serviceCache[i].isValid && _wcsicmp(g_serviceCache[i].address, address) == 0) {
            if (now < g_serviceCache[i].expiresAt) {
                DWORD count = g_serviceCache[i].serviceCount;
                if (count > maxGuids) count = maxGuids;
                memcpy(outGuids, g_serviceCache[i].services, count * sizeof(GUID));
                LeaveCriticalSection(&g_cacheLock);
                return count;
            }
            g_serviceCache[i].isValid = false;
            break;
        }
    }

    LeaveCriticalSection(&g_cacheLock);

    // Not cached or expired: query BluetoothEnumerateInstalledServices
    DWORD count = MAX_CACHED_SERVICES;
    GUID tempGuids[MAX_CACHED_SERVICES];
    DWORD res = BluetoothEnumerateInstalledServices(NULL, info, &count, tempGuids);
    if (res == ERROR_MORE_DATA && count > MAX_CACHED_SERVICES) {
        count = MAX_CACHED_SERVICES;
        res = BluetoothEnumerateInstalledServices(NULL, info, &count, tempGuids);
    }

    if (res != ERROR_SUCCESS) {
        count = 0;
    }

    EnterCriticalSection(&g_cacheLock);
    int slot = -1;
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (!g_serviceCache[i].isValid || now >= g_serviceCache[i].expiresAt) {
            slot = i;
            break;
        }
    }
    if (slot == -1) slot = 0;

    wcscpy_s(g_serviceCache[slot].address, 24, address);
    g_serviceCache[slot].expiresAt = now + CACHE_TTL_MS;
    g_serviceCache[slot].serviceCount = count;
    memcpy(g_serviceCache[slot].services, tempGuids, count * sizeof(GUID));
    g_serviceCache[slot].isValid = true;

    DWORD returnCount = count > maxGuids ? maxGuids : count;
    memcpy(outGuids, tempGuids, returnCount * sizeof(GUID));

    LeaveCriticalSection(&g_cacheLock);
    return returnCount;
}

static bool guid_matches(const GUID* a, const GUID* b) {
    return (memcmp(a, b, sizeof(GUID)) == 0);
}

typedef struct {
    wchar_t deviceId[512];
    wchar_t friendlyName[256];
} ActiveAudioEndpoint;

static int get_active_bluetooth_audio_endpoints(ActiveAudioEndpoint* outList, int maxList) {
    if (!outList || maxList <= 0) return 0;

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool mustUninit = (hrCo == S_OK || hrCo == S_FALSE);

    IMMDeviceEnumerator* pEnumerator = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_Bt, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_Bt, (void**)&pEnumerator);
    if (FAILED(hr) || !pEnumerator) {
        if (mustUninit) CoUninitialize();
        return -1;
    }

    IMMDeviceCollection* pDevices = NULL;
    hr = pEnumerator->lpVtbl->EnumAudioEndpoints(pEnumerator, eAll, DEVICE_STATE_ACTIVE, &pDevices);
    if (FAILED(hr) || !pDevices) {
        pEnumerator->lpVtbl->Release(pEnumerator);
        if (mustUninit) CoUninitialize();
        return -1;
    }

    UINT devCount = 0;
    pDevices->lpVtbl->GetCount(pDevices, &devCount);
    int activeCount = 0;

    for (UINT i = 0; i < devCount && activeCount < maxList; i++) {
        IMMDevice* pDevice = NULL;
        if (FAILED(pDevices->lpVtbl->Item(pDevices, i, &pDevice)) || !pDevice) continue;

        WCHAR friendlyName[256] = L"";
        IPropertyStore* pPropStore = NULL;
        if (SUCCEEDED(pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pPropStore)) && pPropStore) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(pPropStore->lpVtbl->GetValue(pPropStore, &PKEY_Device_FriendlyName_Bt, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                wcsncpy_s(friendlyName, 256, pv.pwszVal, _TRUNCATE);
            }
            PropVariantClear(&pv);
            pPropStore->lpVtbl->Release(pPropStore);
        }

        IDeviceTopology* pTopology = NULL;
        hr = pDevice->lpVtbl->Activate(pDevice, &IID_IDeviceTopology_Bt, CLSCTX_ALL, NULL, (void**)&pTopology);
        if (SUCCEEDED(hr) && pTopology) {
            UINT connCount = 0;
            pTopology->lpVtbl->GetConnectorCount(pTopology, &connCount);
            for (UINT c = 0; c < connCount; c++) {
                IConnector* pConnector = NULL;
                if (FAILED(pTopology->lpVtbl->GetConnector(pTopology, c, &pConnector)) || !pConnector) continue;

                IConnector* pOtherConnector = NULL;
                hr = pConnector->lpVtbl->GetConnectedTo(pConnector, &pOtherConnector);
                if (SUCCEEDED(hr) && pOtherConnector) {
                    IPart* pPart = NULL;
                    hr = pOtherConnector->lpVtbl->QueryInterface(pOtherConnector, &IID_IPart_Bt, (void**)&pPart);
                    if (SUCCEEDED(hr) && pPart) {
                        IDeviceTopology* pOtherTopology = NULL;
                        hr = pPart->lpVtbl->GetTopologyObject(pPart, &pOtherTopology);
                        if (SUCCEEDED(hr) && pOtherTopology) {
                            LPWSTR otherDeviceId = NULL;
                            hr = pOtherTopology->lpVtbl->GetDeviceId(pOtherTopology, &otherDeviceId);
                            if (SUCCEEDED(hr) && otherDeviceId) {
                                bool isBt = (_wcsnicmp(otherDeviceId, L"{2}.\\\\?\\bth", 10) == 0 ||
                                             wcsstr(otherDeviceId, L"bthenum") != NULL ||
                                             wcsstr(otherDeviceId, L"bthhfenum") != NULL ||
                                             wcsstr(otherDeviceId, L"BTH") != NULL);
                                if (isBt) {
                                    wcsncpy_s(outList[activeCount].friendlyName, 256, friendlyName, _TRUNCATE);
                                    wchar_t lowerOtherId[512];
                                    wcsncpy_s(lowerOtherId, 512, otherDeviceId, _TRUNCATE);
                                    for (int k = 0; lowerOtherId[k]; k++) {
                                        lowerOtherId[k] = (wchar_t)towlower(lowerOtherId[k]);
                                    }
                                    wcsncpy_s(outList[activeCount].deviceId, 512, lowerOtherId, _TRUNCATE);
                                    activeCount++;
                                }
                                CoTaskMemFree(otherDeviceId);
                            }
                            pOtherTopology->lpVtbl->Release(pOtherTopology);
                        }
                        pPart->lpVtbl->Release(pPart);
                    }
                    pOtherConnector->lpVtbl->Release(pOtherConnector);
                }
                pConnector->lpVtbl->Release(pConnector);
            }
            pTopology->lpVtbl->Release(pTopology);
        }
        pDevice->lpVtbl->Release(pDevice);
    }

    pDevices->lpVtbl->Release(pDevices);
    pEnumerator->lpVtbl->Release(pEnumerator);
    if (mustUninit) CoUninitialize();
    return activeCount;
}

static bool is_device_audio_active(const wchar_t* address, const wchar_t* name) {
    ActiveAudioEndpoint activeEndpoints[32];
    int count = get_active_bluetooth_audio_endpoints(activeEndpoints, 32);
    if (count <= 0) return false;

    wchar_t cleanMac[24] = { 0 };
    if (address) {
        int idx = 0;
        for (int i = 0; address[i] && idx < 23; i++) {
            if (address[i] != L':') {
                cleanMac[idx++] = (wchar_t)towlower(address[i]);
            }
        }
        cleanMac[idx] = L'\0';
    }

    for (int i = 0; i < count; i++) {
        if (cleanMac[0] != L'\0' && wcsstr(activeEndpoints[i].deviceId, cleanMac) != NULL) {
            return true;
        }
        if (name && name[0] != L'\0' && activeEndpoints[i].friendlyName[0] != L'\0') {
            if (wcsstr(activeEndpoints[i].friendlyName, name) != NULL ||
                wcsstr(name, activeEndpoints[i].friendlyName) != NULL) {
                return true;
            }
        }
    }
    return false;
}

int bt_discover_audio_devices(BluetoothAudioDevice* outDevices, int maxDevices) {
    bt_init();
    if (!outDevices || maxDevices <= 0) return 0;

    ActiveAudioEndpoint activeEndpoints[32];
    int activeEndpointCount = get_active_bluetooth_audio_endpoints(activeEndpoints, 32);

    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = { 0 };
    searchParams.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnConnected = TRUE;
    searchParams.fReturnUnknown = FALSE;
    searchParams.fIssueInquiry = FALSE;
    searchParams.cTimeoutMultiplier = 0;
    searchParams.hRadio = NULL;

    BLUETOOTH_DEVICE_INFO deviceInfo = { 0 };
    deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);

    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
    if (!hFind) return 0;

    int deviceCount = 0;

    do {
        if (deviceInfo.szName[0] == L'\0') {
            deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
            continue;
        }

        wchar_t addrStr[24];
        bt_format_address(deviceInfo.Address.ullLong, addrStr, sizeof(addrStr) / sizeof(wchar_t));

        GUID services[MAX_CACHED_SERVICES];
        DWORD sCount = get_installed_services_cached(&deviceInfo, addrStr, services, MAX_CACHED_SERVICES);

        bool hasA2DP = false;
        bool hasHFP = false;
        for (DWORD i = 0; i < sCount; i++) {
            if (guid_matches(&services[i], &GUID_AudioSink)) hasA2DP = true;
            if (guid_matches(&services[i], &GUID_Handsfree)) hasHFP = true;
        }

        DWORD majorClass = (deviceInfo.ulClassofDevice & 0x1F00) >> 8;
        bool isAudio = hasA2DP || hasHFP || (majorClass == 0x04);

        if (isAudio && deviceCount < maxDevices) {
            BluetoothAudioDevice* dev = &outDevices[deviceCount];
            memset(dev, 0, sizeof(BluetoothAudioDevice));
            wcscpy_s(dev->name, 248, deviceInfo.szName);
            wcscpy_s(dev->displayName, 260, deviceInfo.szName);
            wcscpy_s(dev->address, 24, addrStr);
            dev->rawAddress = deviceInfo.Address.ullLong;
            dev->supportsAudioSink = hasA2DP;
            dev->supportsHandsfree = hasHFP;

            bool isConnected = false;
            if (deviceInfo.fConnected) {
                if (activeEndpointCount >= 0) {
                    wchar_t cleanMac[24] = { 0 };
                    int cIdx = 0;
                    for (int k = 0; addrStr[k] && cIdx < 23; k++) {
                        if (addrStr[k] != L':') {
                            cleanMac[cIdx++] = (wchar_t)towlower(addrStr[k]);
                        }
                    }
                    cleanMac[cIdx] = L'\0';

                    for (int a = 0; a < activeEndpointCount; a++) {
                        if (cleanMac[0] != L'\0' && wcsstr(activeEndpoints[a].deviceId, cleanMac) != NULL) {
                            isConnected = true;
                            break;
                        }
                        if (deviceInfo.szName[0] != L'\0' && activeEndpoints[a].friendlyName[0] != L'\0') {
                            if (wcsstr(activeEndpoints[a].friendlyName, deviceInfo.szName) != NULL ||
                                wcsstr(deviceInfo.szName, activeEndpoints[a].friendlyName) != NULL) {
                                isConnected = true;
                                break;
                            }
                        }
                    }
                } else {
                    isConnected = true;
                }
            }
            dev->isConnected = isConnected;
            deviceCount++;
        }

        deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
    } while (BluetoothFindNextDevice(hFind, &deviceInfo));

    BluetoothFindDeviceClose(hFind);

    // Disambiguate duplicate names
    for (int i = 0; i < deviceCount; i++) {
        int dupCount = 0;
        for (int j = 0; j < deviceCount; j++) {
            if (_wcsicmp(outDevices[i].name, outDevices[j].name) == 0) {
                dupCount++;
            }
        }
        if (dupCount > 1) {
            swprintf_s(outDevices[i].displayName, 260, L"%s (%s)", outDevices[i].name, outDevices[i].address);
        }
    }

    return deviceCount;
}

static bool find_device_info(const wchar_t* address, BLUETOOTH_DEVICE_INFO* outInfo) {
    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = { 0 };
    searchParams.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnConnected = TRUE;
    searchParams.fReturnUnknown = FALSE;
    searchParams.hRadio = NULL;

    outInfo->dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, outInfo);
    if (!hFind) return false;

    bool found = false;
    do {
        wchar_t addrStr[24];
        bt_format_address(outInfo->Address.ullLong, addrStr, 24);
        if (_wcsicmp(addrStr, address) == 0) {
            found = true;
            break;
        }
        outInfo->dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
    } while (BluetoothFindNextDevice(hFind, outInfo));

    BluetoothFindDeviceClose(hFind);
    return found;
}

bool bt_get_connection_state(const wchar_t* address, bool* isConnected) {
    BLUETOOTH_DEVICE_INFO info = { 0 };
    if (find_device_info(address, &info)) {
        if (isConnected) *isConnected = (info.fConnected != FALSE);
        return true;
    }
    if (isConnected) *isConnected = false;
    return false;
}

bool bt_connect_device_api(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result) {
    bt_init();
    if (result) {
        wcscpy_s(result->deviceName, 248, name ? name : address);
        wcscpy_s(result->deviceAddress, 24, address);
        result->outcome = TOGGLE_FAILED;
        wcscpy_s(result->message, 256, L"Unknown error.");
    }

    BLUETOOTH_DEVICE_INFO info = { 0 };
    if (!find_device_info(address, &info)) {
        if (result) wcscpy_s(result->message, 256, L"Device not found.");
        return false;
    }

    GUID services[MAX_CACHED_SERVICES];
    DWORD count = get_installed_services_cached(&info, address, services, MAX_CACHED_SERVICES);

    GUID targetGuids[2];
    int targetCount = 0;
    bool hasA2DP = false, hasHFP = false;
    for (DWORD i = 0; i < count; i++) {
        if (guid_matches(&services[i], &GUID_AudioSink)) hasA2DP = true;
        if (guid_matches(&services[i], &GUID_Handsfree)) hasHFP = true;
    }

    if (hasA2DP) targetGuids[targetCount++] = GUID_AudioSink;
    if (hasHFP) targetGuids[targetCount++] = GUID_Handsfree;
    if (targetCount == 0) {
        targetGuids[targetCount++] = GUID_AudioSink;
        targetGuids[targetCount++] = GUID_Handsfree;
    }

    bool allOk = true;
    for (int i = 0; i < targetCount; i++) {
        GUID g = targetGuids[i];
        DWORD r = BluetoothSetServiceState(NULL, &info, &g, 1);
        if (r != ERROR_SUCCESS) {
            // Reset cycle: toggle 0 then 1
            BluetoothSetServiceState(NULL, &info, &g, 0);
            r = BluetoothSetServiceState(NULL, &info, &g, 1);
            if (r != ERROR_SUCCESS) {
                allOk = false;
            }
        }
    }

    if (!allOk) {
        invalidate_service_cache(address);
        if (result) {
            result->outcome = TOGGLE_FAILED;
            wcscpy_s(result->message, 256, L"Service-state change failed.");
        }
        return false;
    }

    if (result) {
        result->outcome = TOGGLE_CONNECTED;
        wcscpy_s(result->message, 256, L"Audio services enabled.");
    }
    return true;
}

bool bt_disconnect_device_api(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result) {
    bt_init();
    if (result) {
        wcscpy_s(result->deviceName, 248, name ? name : address);
        wcscpy_s(result->deviceAddress, 24, address);
        result->outcome = TOGGLE_FAILED;
        wcscpy_s(result->message, 256, L"Unknown error.");
    }

    BLUETOOTH_DEVICE_INFO info = { 0 };
    if (!find_device_info(address, &info)) {
        if (result) wcscpy_s(result->message, 256, L"Device not found.");
        return false;
    }

    GUID services[MAX_CACHED_SERVICES];
    DWORD count = get_installed_services_cached(&info, address, services, MAX_CACHED_SERVICES);

    GUID targetGuids[2];
    int targetCount = 0;
    bool hasA2DP = false, hasHFP = false;
    for (DWORD i = 0; i < count; i++) {
        if (guid_matches(&services[i], &GUID_AudioSink)) hasA2DP = true;
        if (guid_matches(&services[i], &GUID_Handsfree)) hasHFP = true;
    }

    if (hasA2DP) targetGuids[targetCount++] = GUID_AudioSink;
    if (hasHFP) targetGuids[targetCount++] = GUID_Handsfree;
    if (targetCount == 0) {
        targetGuids[targetCount++] = GUID_AudioSink;
        targetGuids[targetCount++] = GUID_Handsfree;
    }

    bool allOk = true;
    for (int i = 0; i < targetCount; i++) {
        GUID g = targetGuids[i];
        DWORD r = BluetoothSetServiceState(NULL, &info, &g, 0);
        if (r != ERROR_SUCCESS && r != 1168 /* ERROR_NOT_FOUND */) {
            allOk = false;
        }
    }

    bool stillConnected = false;
    if (bt_get_connection_state(address, &stillConnected) && stillConnected) {
        allOk = false;
    }

    if (!allOk) {
        invalidate_service_cache(address);
        if (result) {
            result->outcome = TOGGLE_FAILED;
            wcscpy_s(result->message, 256,
                L"Audio services disabled, but device is still connected. Use HCI disconnect for full link drop.");
        }
        return false;
    }

    if (result) {
        result->outcome = TOGGLE_DISCONNECTED;
        wcscpy_s(result->message, 256, L"Audio services disabled.");
    }
    return true;
}

bool bt_disconnect_device_hci(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result) {
    if (result) {
        wcscpy_s(result->deviceName, 248, name ? name : address);
        wcscpy_s(result->deviceAddress, 24, address);
        result->outcome = TOGGLE_FAILED;
        wcscpy_s(result->message, 256, L"Unknown error.");
    }

    BLUETOOTH_FIND_RADIO_PARAMS rfparams = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
    HANDLE hRadio = NULL;
    HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&rfparams, &hRadio);
    if (!hFind || !hRadio) {
        if (result) wcscpy_s(result->message, 256, L"No Bluetooth radio found.");
        return false;
    }

    ULONGLONG addr = bt_parse_address(address);
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hRadio, IOCTL_BTH_DISCONNECT_DEVICE, &addr, sizeof(addr), NULL, 0, &bytesReturned, NULL);
    DWORD lastErr = ok ? ERROR_SUCCESS : GetLastError();

    CloseHandle(hRadio);
    BluetoothFindRadioClose(hFind);

    if (!ok) {
        if (result) {
            result->outcome = TOGGLE_FAILED;
            swprintf_s(result->message, 256, L"HCI disconnect failed. Win32 error %lu.", lastErr);
        }
        return false;
    }

    if (result) {
        result->outcome = TOGGLE_DISCONNECTED;
        wcscpy_s(result->message, 256, L"Disconnected via HCI IOCTL.");
    }
    return true;
}

static bool bt_toggle_device_ks(const wchar_t* address, const wchar_t* name, bool isConnect, DeviceToggleResult* result) {
    bt_init();
    if (result) {
        wcscpy_s(result->deviceName, 248, name ? name : address);
        wcscpy_s(result->deviceAddress, 24, address);
        result->outcome = TOGGLE_FAILED;
        wcscpy_s(result->message, 256, L"Unknown error.");
    }

    // Build clean lowercase MAC without colons
    wchar_t cleanMac[24] = { 0 };
    if (address) {
        int idx = 0;
        for (int i = 0; address[i] && idx < 23; i++) {
            wchar_t c = address[i];
            if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')) {
                cleanMac[idx++] = (wchar_t)towlower(c);
            }
        }
        cleanMac[idx] = L'\0';
    }

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool mustUninit = (hrCo == S_OK || hrCo == S_FALSE);

    IMMDeviceEnumerator* pEnumerator = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_Bt, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_Bt, (void**)&pEnumerator);
    if (FAILED(hr) || !pEnumerator) {
        if (mustUninit) CoUninitialize();
        if (isConnect) return bt_connect_device_api(address, name, result);
        else return bt_disconnect_device_api(address, name, result);
    }

    IMMDeviceCollection* pDevices = NULL;
    hr = pEnumerator->lpVtbl->EnumAudioEndpoints(pEnumerator, eAll, DEVICE_STATEMASK_ALL, &pDevices);
    if (FAILED(hr) || !pDevices) {
        pEnumerator->lpVtbl->Release(pEnumerator);
        if (mustUninit) CoUninitialize();
        if (isConnect) return bt_connect_device_api(address, name, result);
        else return bt_disconnect_device_api(address, name, result);
    }

    UINT devCount = 0;
    pDevices->lpVtbl->GetCount(pDevices, &devCount);

    int ksCommandsSent = 0;
    int ksSuccesses = 0;

    for (UINT i = 0; i < devCount; i++) {
        IMMDevice* pDevice = NULL;
        if (FAILED(pDevices->lpVtbl->Item(pDevices, i, &pDevice)) || !pDevice) continue;

        WCHAR friendlyName[256] = L"";
        IPropertyStore* pPropStore = NULL;
        if (SUCCEEDED(pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pPropStore)) && pPropStore) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(pPropStore->lpVtbl->GetValue(pPropStore, &PKEY_Device_FriendlyName_Bt, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                wcsncpy_s(friendlyName, 256, pv.pwszVal, _TRUNCATE);
            }
            PropVariantClear(&pv);
            pPropStore->lpVtbl->Release(pPropStore);
        }

        IDeviceTopology* pTopology = NULL;
        hr = pDevice->lpVtbl->Activate(pDevice, &IID_IDeviceTopology_Bt, CLSCTX_ALL, NULL, (void**)&pTopology);
        if (SUCCEEDED(hr) && pTopology) {
            UINT connCount = 0;
            pTopology->lpVtbl->GetConnectorCount(pTopology, &connCount);
            for (UINT c = 0; c < connCount; c++) {
                IConnector* pConnector = NULL;
                if (FAILED(pTopology->lpVtbl->GetConnector(pTopology, c, &pConnector)) || !pConnector) continue;

                IConnector* pOtherConnector = NULL;
                hr = pConnector->lpVtbl->GetConnectedTo(pConnector, &pOtherConnector);
                if (SUCCEEDED(hr) && pOtherConnector) {
                    IPart* pPart = NULL;
                    hr = pOtherConnector->lpVtbl->QueryInterface(pOtherConnector, &IID_IPart_Bt, (void**)&pPart);
                    if (SUCCEEDED(hr) && pPart) {
                        IDeviceTopology* pOtherTopology = NULL;
                        hr = pPart->lpVtbl->GetTopologyObject(pPart, &pOtherTopology);
                        if (SUCCEEDED(hr) && pOtherTopology) {
                            LPWSTR otherDeviceId = NULL;
                            hr = pOtherTopology->lpVtbl->GetDeviceId(pOtherTopology, &otherDeviceId);
                            if (SUCCEEDED(hr) && otherDeviceId) {
                                // Check if this is a Bluetooth audio device
                                bool isBt = (_wcsnicmp(otherDeviceId, L"{2}.\\\\?\\bth", 10) == 0 ||
                                             wcsstr(otherDeviceId, L"bthenum") != NULL ||
                                             wcsstr(otherDeviceId, L"bthhfenum") != NULL ||
                                             wcsstr(otherDeviceId, L"BTH") != NULL);
                                if (isBt) {
                                    bool matchesDevice = false;

                                    // 1. MAC address match in device ID
                                    if (cleanMac[0] != L'\0') {
                                        wchar_t lowerOtherId[512];
                                        wcsncpy_s(lowerOtherId, 512, otherDeviceId, _TRUNCATE);
                                        for (int k = 0; lowerOtherId[k]; k++) {
                                            lowerOtherId[k] = (wchar_t)towlower(lowerOtherId[k]);
                                        }
                                        if (wcsstr(lowerOtherId, cleanMac) != NULL) {
                                            matchesDevice = true;
                                        }
                                    }

                                    // 2. Friendly name match fallback
                                    if (!matchesDevice && name && name[0] != L'\0' && friendlyName[0] != L'\0') {
                                        if (wcsstr(friendlyName, name) != NULL || wcsstr(name, friendlyName) != NULL) {
                                            matchesDevice = true;
                                        }
                                    }

                                    if (matchesDevice) {
                                        IMMDevice* pOtherDevice = NULL;
                                        hr = pEnumerator->lpVtbl->GetDevice(pEnumerator, otherDeviceId, &pOtherDevice);
                                        if (SUCCEEDED(hr) && pOtherDevice) {
                                            IKsControl* pKsControl = NULL;
                                            hr = pOtherDevice->lpVtbl->Activate(pOtherDevice, &IID_IKsControl_Bt, CLSCTX_ALL, NULL, (void**)&pKsControl);
                                            if (SUCCEEDED(hr) && pKsControl) {
                                                KSPROPERTY prop;
                                                memset(&prop, 0, sizeof(prop));
                                                prop.Set = KSPROPSETID_BtAudio_Bt;
                                                prop.Id = isConnect ? KSPROPERTY_ONESHOT_RECONNECT : KSPROPERTY_ONESHOT_DISCONNECT;
                                                prop.Flags = KSPROPERTY_TYPE_GET;

                                                ULONG bytesReturned = 0;
                                                HRESULT ksHr = pKsControl->lpVtbl->KsProperty(pKsControl, &prop, sizeof(prop), NULL, 0, &bytesReturned);
                                                ksCommandsSent++;
                                                if (SUCCEEDED(ksHr)) {
                                                    ksSuccesses++;
                                                }
                                                pKsControl->lpVtbl->Release(pKsControl);
                                            }
                                            pOtherDevice->lpVtbl->Release(pOtherDevice);
                                        }
                                    }
                                }
                                CoTaskMemFree(otherDeviceId);
                            }
                            pOtherTopology->lpVtbl->Release(pOtherTopology);
                        }
                        pPart->lpVtbl->Release(pPart);
                    }
                    pOtherConnector->lpVtbl->Release(pOtherConnector);
                }
                pConnector->lpVtbl->Release(pConnector);
            }
            pTopology->lpVtbl->Release(pTopology);
        }
        pDevice->lpVtbl->Release(pDevice);
    }

    pDevices->lpVtbl->Release(pDevices);
    pEnumerator->lpVtbl->Release(pEnumerator);
    if (mustUninit) CoUninitialize();

    if (ksSuccesses > 0) {
        if (isConnect) {
            // Keep busy state and flashing LED active while connection is being verified!
            // Poll for audio endpoint to become DEVICE_STATE_ACTIVE (up to ~3.6 seconds)
            bool verified = false;
            for (int poll = 0; poll < 24; poll++) {
                Sleep(150);
                if (is_device_audio_active(address, name)) {
                    verified = true;
                    break;
                }
            }

            if (verified) {
                if (result) {
                    result->outcome = TOGGLE_CONNECTED;
                    wcscpy_s(result->message, 256, L"Connected via KS audio driver.");
                }
                return true;
            }

            // If KS reconnect didn't activate the audio endpoint within timeout,
            // fallback to Win32 API to complete the connection
            return bt_connect_device_api(address, name, result);
        } else {
            // For disconnect: verify audio endpoint drops from active (up to ~1.2 seconds)
            for (int poll = 0; poll < 8; poll++) {
                if (!is_device_audio_active(address, name)) {
                    break;
                }
                Sleep(150);
            }

            if (result) {
                result->outcome = TOGGLE_DISCONNECTED;
                wcscpy_s(result->message, 256, L"Disconnected via KS audio driver.");
            }
            return true;
        }
    }

    // If no KS command could be sent or all returned failure, fallback to Win32 API
    if (isConnect) {
        return bt_connect_device_api(address, name, result);
    } else {
        return bt_disconnect_device_api(address, name, result);
    }
}

bool bt_connect_device_ks(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result) {
    return bt_toggle_device_ks(address, name, true, result);
}

bool bt_disconnect_device_ks(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result) {
    return bt_toggle_device_ks(address, name, false, result);
}

