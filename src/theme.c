#include "theme.h"

static const wchar_t* REG_KEY_THEMES = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static const wchar_t* VAL_LIGHT_THEME = L"AppsUseLightTheme";

bool theme_is_dark_mode(void) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_THEMES, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD val = 1;
    DWORD valSize = sizeof(val);
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(hKey, VAL_LIGHT_THEME, NULL, &type, (LPBYTE)&val, &valSize);
    RegCloseKey(hKey);

    if (status == ERROR_SUCCESS && type == REG_DWORD) {
        return (val == 0);
    }
    return false;
}

void theme_get_colors(ThemeColors* c) {
    if (!c) return;
    c->isDark = theme_is_dark_mode();

    if (c->isDark) {
        c->bg = RGB(43, 43, 43);             // #2B2B2B
        c->fg = RGB(240, 240, 240);          // #F0F0F0
        c->rowHover = RGB(62, 62, 62);       // #3E3E3E
        c->btnBg = RGB(60, 60, 60);          // #3C3C3C
        c->btnHover = RGB(80, 80, 80);       // #505050
        c->btnPressed = RGB(102, 102, 102);  // #666666
        c->btnBorder = RGB(85, 85, 85);      // #555555
        c->separator = RGB(72, 72, 72);      // #484848
        c->subtext = RGB(170, 170, 170);
        c->accent = RGB(10, 102, 216);       // #0A66D8
        c->statusConnected = RGB(46, 204, 113); // Green dot
        c->statusDisconnected = RGB(130, 130, 130);
        c->checkBg = RGB(60, 60, 60);
        c->checkBorder = RGB(153, 153, 153);
        c->checkMark = RGB(240, 240, 240);
    } else {
        c->bg = RGB(249, 249, 249);
        c->fg = RGB(26, 26, 26);
        c->rowHover = RGB(232, 232, 232);
        c->btnBg = RGB(240, 240, 240);
        c->btnHover = RGB(225, 225, 225);
        c->btnPressed = RGB(210, 210, 210);
        c->btnBorder = RGB(190, 190, 190);
        c->separator = RGB(218, 218, 218);
        c->subtext = RGB(110, 110, 110);
        c->accent = RGB(10, 102, 216);
        c->statusConnected = RGB(39, 174, 96);
        c->statusDisconnected = RGB(160, 160, 160);
        c->checkBg = RGB(255, 255, 255);
        c->checkBorder = RGB(180, 180, 180);
        c->checkMark = RGB(26, 26, 26);
    }
}

