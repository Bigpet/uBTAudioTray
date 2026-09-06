#include "config.h"
#include <windows.h>
#include "media.h"

#define VK_MEDIA_PLAY_PAUSE 0xB3

bool media_send_toggle(void) {
    INPUT inputs[2] = { 0 };

    // Key down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_MEDIA_PLAY_PAUSE;
    inputs[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;

    // Key up
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_MEDIA_PLAY_PAUSE;
    inputs[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;

    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    return (sent == 2);
}

