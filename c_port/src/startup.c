#include "startup.h"
#include <stdio.h>

static const wchar_t* REG_KEY_RUN = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* APP_NAME = L"QuickBTTray";

bool startup_is_enabled(void) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_RUN, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH * 2] = { 0 };
    DWORD dataSize = sizeof(value);
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)value, &dataSize);
    RegCloseKey(hKey);

    return (status == ERROR_SUCCESS && dataSize > sizeof(wchar_t));
}

void startup_set_enabled(bool enable) {
    HKEY hKey = NULL;
    if (enable) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_RUN, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            wchar_t exePath[MAX_PATH] = { 0 };
            GetModuleFileNameW(NULL, exePath, MAX_PATH);

            wchar_t cmdLine[MAX_PATH + 4];
            swprintf_s(cmdLine, MAX_PATH + 4, L"\"%s\"", exePath);

            DWORD sizeInBytes = (DWORD)((wcslen(cmdLine) + 1) * sizeof(wchar_t));
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)cmdLine, sizeInBytes);
            RegCloseKey(hKey);
        }
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_RUN, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, APP_NAME);
            RegCloseKey(hKey);
        }
    }
}

