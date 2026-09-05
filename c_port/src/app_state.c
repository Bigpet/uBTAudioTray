#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

static void get_state_path(wchar_t* outPath, size_t maxLen) {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    }
    swprintf_s(outPath, maxLen, L"%s\\uBTAudioTray\\tray-state.json", localAppData);
}

static void get_legacy_state_path(wchar_t* outPath, size_t maxLen) {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    }
    swprintf_s(outPath, maxLen, L"%s\\QuickBTTray\\tray-state.json", localAppData);
}

static void ensure_dir_exists(const wchar_t* filePath) {
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, filePath);
    wchar_t* lastSlash = wcsrchr(dir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        CreateDirectoryW(dir, NULL);
    }
}

void app_state_init_default(AppState* state) {
    if (!state) return;
    memset(state, 0, sizeof(AppState));
    state->enableNotifications = true;
    state->sendMediaPlayOnConnect = false;
    state->sendMediaPauseOnDisconnect = false;
    state->connectMethod = CONNECT_METHOD_KS;
    state->useUiaConnect = false;
    state->useUiaDisconnect = false;
    state->useHciDisconnect = true;
    state->selectedCount = 0;
}

static bool parse_json_bool(const char* json, const char* key, bool defaultVal) {
    const char* pos = strstr(json, key);
    if (!pos) return defaultVal;
    pos = strchr(pos, ':');
    if (!pos) return defaultVal;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':') pos++;
    if (_strnicmp(pos, "true", 4) == 0) return true;
    if (_strnicmp(pos, "false", 5) == 0) return false;
    return defaultVal;
}

static bool parse_json_string(const char* json, const char* key, char* outVal, size_t maxLen) {
    if (!outVal || maxLen == 0) return false;
    outVal[0] = '\0';
    const char* pos = strstr(json, key);
    if (!pos) return false;
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos = strchr(pos, '\"');
    if (!pos) return false;
    pos++;
    const char* end = strchr(pos, '\"');
    if (!end) return false;
    size_t len = (size_t)(end - pos);
    if (len >= maxLen) len = maxLen - 1;
    memcpy(outVal, pos, len);
    outVal[len] = '\0';
    return true;
}

static void parse_json_string_array(const char* json, const char* key, wchar_t outArray[MAX_SELECTED_DEVICES][MAC_ADDR_LEN], int* outCount) {
    *outCount = 0;
    const char* pos = strstr(json, key);
    if (!pos) return;
    pos = strchr(pos, '[');
    if (!pos) return;
    pos++;

    while (*pos && *pos != ']' && *outCount < MAX_SELECTED_DEVICES) {
        while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',')) pos++;
        if (*pos == '\"') {
            pos++;
            const char* end = strchr(pos, '\"');
            if (end) {
                size_t len = (size_t)(end - pos);
                if (len > 0 && len < MAC_ADDR_LEN) {
                    char temp[MAC_ADDR_LEN] = { 0 };
                    memcpy(temp, pos, len);
                    temp[len] = '\0';
                    MultiByteToWideChar(CP_UTF8, 0, temp, -1, outArray[*outCount], MAC_ADDR_LEN);
                    (*outCount)++;
                }
                pos = end + 1;
            } else {
                break;
            }
        } else {
            pos++;
        }
    }
}

bool app_state_load(AppState* state) {
    app_state_init_default(state);

    wchar_t path[MAX_PATH];
    get_state_path(path, MAX_PATH);

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        get_legacy_state_path(path, MAX_PATH);
        hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0 || fileSize > 65536) {
        CloseHandle(hFile);
        return false;
    }

    char* buffer = (char*)malloc(fileSize + 1);
    if (!buffer) {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer, fileSize, &bytesRead, NULL) || bytesRead == 0) {
        free(buffer);
        CloseHandle(hFile);
        return false;
    }
    buffer[bytesRead] = '\0';
    CloseHandle(hFile);

    parse_json_string_array(buffer, "SelectedDeviceAddresses", state->selectedAddresses, &state->selectedCount);
    state->enableNotifications = parse_json_bool(buffer, "EnableNotifications", true);
    state->sendMediaPlayOnConnect = parse_json_bool(buffer, "SendMediaPlayOnConnect", false);
    state->sendMediaPauseOnDisconnect = parse_json_bool(buffer, "SendMediaPauseOnDisconnect", false);
    state->useUiaConnect = parse_json_bool(buffer, "UseUiaConnect", false);
    state->useUiaDisconnect = parse_json_bool(buffer, "UseUiaDisconnect", false);
    state->useHciDisconnect = parse_json_bool(buffer, "UseHciDisconnect", true);

    char methodStr[32] = { 0 };
    if (parse_json_string(buffer, "ConnectMethod", methodStr, sizeof(methodStr))) {
        if (_stricmp(methodStr, "API") == 0) {
            state->connectMethod = CONNECT_METHOD_API;
        } else if (_stricmp(methodStr, "UI") == 0) {
            state->connectMethod = CONNECT_METHOD_UI;
        } else {
            state->connectMethod = CONNECT_METHOD_KS;
        }
    } else {
        state->connectMethod = state->useUiaConnect ? CONNECT_METHOD_UI : CONNECT_METHOD_KS;
    }
    state->useUiaConnect = (state->connectMethod == CONNECT_METHOD_UI);

    free(buffer);
    return true;
}

bool app_state_save(const AppState* state) {
    if (!state) return false;

    wchar_t path[MAX_PATH];
    get_state_path(path, MAX_PATH);
    ensure_dir_exists(path);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    char buffer[4096];
    int offset = 0;

    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "{\n  \"SelectedDeviceAddresses\": [\n");
    for (int i = 0; i < state->selectedCount; i++) {
        char addrA[MAC_ADDR_LEN] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, state->selectedAddresses[i], -1, addrA, sizeof(addrA), NULL, NULL);
        offset += sprintf_s(buffer + offset, sizeof(buffer) - offset,
            "    \"%s\"%s\n",
            addrA,
            (i + 1 < state->selectedCount) ? "," : "");
    }
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "  ],\n");

    const char* connMethodStr = "KS";
    if (state->connectMethod == CONNECT_METHOD_API) connMethodStr = "API";
    else if (state->connectMethod == CONNECT_METHOD_UI) connMethodStr = "UI";

    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset,
        "  \"EnableNotifications\": %s,\n"
        "  \"SendMediaPlayOnConnect\": %s,\n"
        "  \"SendMediaPauseOnDisconnect\": %s,\n"
        "  \"ConnectMethod\": \"%s\",\n"
        "  \"UseUiaConnect\": %s,\n"
        "  \"UseUiaDisconnect\": %s,\n"
        "  \"UseHciDisconnect\": %s\n"
        "}\n",
        state->enableNotifications ? "true" : "false",
        state->sendMediaPlayOnConnect ? "true" : "false",
        state->sendMediaPauseOnDisconnect ? "true" : "false",
        connMethodStr,
        (state->connectMethod == CONNECT_METHOD_UI) ? "true" : "false",
        state->useUiaDisconnect ? "true" : "false",
        state->useHciDisconnect ? "true" : "false");

    DWORD bytesWritten = 0;
    WriteFile(hFile, buffer, (DWORD)offset, &bytesWritten, NULL);
    CloseHandle(hFile);

    return (bytesWritten == (DWORD)offset);
}

bool app_state_is_selected(const AppState* state, const wchar_t* address) {
    if (!state || !address) return false;
    for (int i = 0; i < state->selectedCount; i++) {
        if (_wcsicmp(state->selectedAddresses[i], address) == 0) {
            return true;
        }
    }
    return false;
}

void app_state_set_selected(AppState* state, const wchar_t* address, bool selected) {
    if (!state || !address) return;

    int foundIdx = -1;
    for (int i = 0; i < state->selectedCount; i++) {
        if (_wcsicmp(state->selectedAddresses[i], address) == 0) {
            foundIdx = i;
            break;
        }
    }

    if (selected && foundIdx == -1) {
        if (state->selectedCount < MAX_SELECTED_DEVICES) {
            wcscpy_s(state->selectedAddresses[state->selectedCount], MAC_ADDR_LEN, address);
            state->selectedCount++;
        }
    } else if (!selected && foundIdx != -1) {
        for (int i = foundIdx; i < state->selectedCount - 1; i++) {
            wcscpy_s(state->selectedAddresses[i], MAC_ADDR_LEN, state->selectedAddresses[i + 1]);
        }
        state->selectedCount--;
    }
}

void app_state_prune_unseen(AppState* state, const wchar_t activeAddresses[][MAC_ADDR_LEN], int activeCount) {
    if (!state) return;
    int writeIdx = 0;
    for (int i = 0; i < state->selectedCount; i++) {
        bool stillExists = false;
        for (int j = 0; j < activeCount; j++) {
            if (_wcsicmp(state->selectedAddresses[i], activeAddresses[j]) == 0) {
                stillExists = true;
                break;
            }
        }
        if (stillExists) {
            if (writeIdx != i) {
                wcscpy_s(state->selectedAddresses[writeIdx], MAC_ADDR_LEN, state->selectedAddresses[i]);
            }
            writeIdx++;
        }
    }
    state->selectedCount = writeIdx;
}

