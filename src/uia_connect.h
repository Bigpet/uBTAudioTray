#pragma once

#include <windows.h>
#include <stdbool.h>
#include "bluetooth.h"
#include "config.h"

#if ENABLE_UI
// Connect / Disconnect via Windows Settings UI Automation
bool uia_connect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, DeviceToggleResult* result);
bool uia_disconnect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, DeviceToggleResult* result);
#endif

