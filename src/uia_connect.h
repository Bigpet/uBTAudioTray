#pragma once

#include <stdbool.h>
#include "config.h"
#include "bluetooth.h"

#if ENABLE_UI
// Connect / Disconnect via Windows Settings UI Automation
bool uia_connect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, bool keepOpen, DeviceToggleResult* result);
bool uia_disconnect_device(const wchar_t* deviceName, const wchar_t* deviceAddress, bool keepOpen, DeviceToggleResult* result);

// Closes Windows Settings if it was opened by uBTAudioTray during an automated session
void uia_close_settings_if_opened(void);
#endif

