#pragma once

#include <windows.h>
#include <bthsdpdef.h>
#include <bluetoothapis.h>
#include <stdbool.h>

#define MAX_BT_DEVICES 32

typedef struct {
    wchar_t name[248];
    wchar_t displayName[260];
    wchar_t address[24];
    ULONGLONG rawAddress;
    bool isConnected;
    bool isSelected;
    bool supportsHandsfree;
    bool supportsAudioSink;
} BluetoothAudioDevice;

typedef enum {
    TOGGLE_CONNECTED,
    TOGGLE_DISCONNECTED,
    TOGGLE_FAILED
} ToggleOutcome;

typedef struct {
    wchar_t deviceName[248];
    wchar_t deviceAddress[24];
    ToggleOutcome outcome;
    wchar_t message[256];
} DeviceToggleResult;

void bt_init(void);
void bt_cleanup(void);

// Enumerate all paired/connected audio devices
int bt_discover_audio_devices(BluetoothAudioDevice* outDevices, int maxDevices);

// Connection operations
bool bt_connect_device_api(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result);
bool bt_connect_device_ks(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result);
bool bt_disconnect_device_api(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result);
bool bt_disconnect_device_hci(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result);
bool bt_disconnect_device_ks(const wchar_t* address, const wchar_t* name, DeviceToggleResult* result);

// Connection check
bool bt_get_connection_state(const wchar_t* address, bool* isConnected);
ULONGLONG bt_parse_address(const wchar_t* address);
void bt_format_address(ULONGLONG addr, wchar_t* outStr, size_t maxLen);

