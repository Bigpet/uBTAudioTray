#include "bluetooth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOCTL_BTH_DISCONNECT_DEVICE 0x41000c
#define CACHE_TTL_MS 45000
#define MAX_CACHED_SERVICES 16
#define MAX_CACHE_ENTRIES 32

static const GUID GUID_Handsfree = { 0x0000111e, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
static const GUID GUID_AudioSink = { 0x0000110b, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };

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

int bt_discover_audio_devices(BluetoothAudioDevice* outDevices, int maxDevices) {
    bt_init();
    if (!outDevices || maxDevices <= 0) return 0;

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
            dev->isConnected = (deviceInfo.fConnected != FALSE);
            dev->supportsAudioSink = hasA2DP;
            dev->supportsHandsfree = hasHFP;
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

