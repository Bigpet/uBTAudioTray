#pragma once

#include "config.h"
#include <windows.h>
#include <stdbool.h>

// Register / unregister for Windows Core Audio endpoint events (IMMNotificationClient)
bool audio_state_notification_register(HWND hWnd, UINT msg);
void audio_state_notification_unregister(void);
