#pragma once

#include <windows.h>
#include <stdbool.h>
#include "bluetooth.h"

// Connect / Disconnect via Windows Settings UI Automation
bool uia_connect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, DeviceToggleResult* result);
bool uia_disconnect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, DeviceToggleResult* result);

