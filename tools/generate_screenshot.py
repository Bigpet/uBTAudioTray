#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
uBTAudioTray Screenshot Generator
Automates:
1. Setting primary monitor display scaling to 175% (or user-specified DPI)
2. Launching uBTAudioTray.exe
3. Opening the tray menu
4. Opening the settings window
5. Re-focusing the menu window
6. Capturing a transparent PNG with authentic Windows DWM window shadows via two-pass differential matting
7. Safely restoring original display scaling and theme settings
"""

import argparse
import ctypes
from ctypes import wintypes
import os
import subprocess
import sys
import time
import winreg
from PIL import Image

# Initialize Win32 APIs
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
dwmapi = ctypes.windll.dwmapi
kernel32 = ctypes.windll.kernel32
shell32 = ctypes.windll.shell32

# Win32 Constants
QDC_ONLY_ACTIVE_PATHS = 0x00000002
DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE = -3
DISPLAYCONFIG_DEVICE_INFO_SET_DPI_SCALE = -4

WM_APP = 0x8000
WM_APP_TRAYMSG = WM_APP + 1
WM_RBUTTONUP = 0x0205
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_CLOSE = 0x0010

WS_POPUP = 0x80000000
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_NOACTIVATE = 0x08000000
WS_EX_TOPMOST = 0x00000008

SWP_NOSIZE = 0x0001
SWP_NOMOVE = 0x0002
SWP_NOACTIVATE = 0x0010
SWP_SHOWWINDOW = 0x0040
HWND_TOPMOST = wintypes.HWND(-1)

GCLP_HBRBACKGROUND = -10
SRCCOPY = 0x00CC0020

DPI_VALS = [100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500]

REG_THEMES = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize"
VAL_LIGHT_THEME = "AppsUseLightTheme"


# Struct Definitions
class LUID(ctypes.Structure):
    _fields_ = [("LowPart", wintypes.DWORD), ("HighPart", wintypes.LONG)]


class DISPLAYCONFIG_RATIONAL(ctypes.Structure):
    _fields_ = [("Numerator", wintypes.UINT), ("Denominator", wintypes.UINT)]


class DISPLAYCONFIG_PATH_SOURCE_INFO(ctypes.Structure):
    _fields_ = [
        ("adapterId", LUID),
        ("id", wintypes.UINT),
        ("modeInfoIdx", wintypes.UINT),
        ("statusFlags", wintypes.UINT),
    ]


class DISPLAYCONFIG_PATH_TARGET_INFO(ctypes.Structure):
    _fields_ = [
        ("adapterId", LUID),
        ("id", wintypes.UINT),
        ("modeInfoIdx", wintypes.UINT),
        ("outputTechnology", wintypes.UINT),
        ("rotation", wintypes.UINT),
        ("scaling", wintypes.UINT),
        ("refreshRate", DISPLAYCONFIG_RATIONAL),
        ("scanLineOrdering", wintypes.UINT),
        ("targetAvailable", wintypes.BOOL),
        ("statusFlags", wintypes.UINT),
    ]


class DISPLAYCONFIG_PATH_INFO(ctypes.Structure):
    _fields_ = [
        ("sourceInfo", DISPLAYCONFIG_PATH_SOURCE_INFO),
        ("targetInfo", DISPLAYCONFIG_PATH_TARGET_INFO),
        ("flags", wintypes.UINT),
    ]


class POINTL(ctypes.Structure):
    _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


class DISPLAYCONFIG_SOURCE_MODE(ctypes.Structure):
    _fields_ = [
        ("width", wintypes.UINT),
        ("height", wintypes.UINT),
        ("pixelFormat", wintypes.UINT),
        ("position", POINTL),
    ]


class _MODE_INFO_UNION(ctypes.Union):
    _fields_ = [
        ("targetMode", ctypes.c_byte * 48),
        ("sourceMode", DISPLAYCONFIG_SOURCE_MODE),
        ("desktopImageInfo", ctypes.c_byte * 36),
    ]


class DISPLAYCONFIG_MODE_INFO(ctypes.Structure):
    _fields_ = [
        ("infoType", wintypes.UINT),
        ("id", wintypes.UINT),
        ("adapterId", LUID),
        ("modeInfo", _MODE_INFO_UNION),
    ]


class DISPLAYCONFIG_DEVICE_INFO_HEADER(ctypes.Structure):
    _fields_ = [
        ("type", wintypes.INT),
        ("size", wintypes.UINT),
        ("adapterId", LUID),
        ("id", wintypes.UINT),
    ]


class DISPLAYCONFIG_SOURCE_DPI_SCALE_GET(ctypes.Structure):
    _fields_ = [
        ("header", DISPLAYCONFIG_DEVICE_INFO_HEADER),
        ("minScaleRel", ctypes.c_int32),
        ("curScaleRel", ctypes.c_int32),
        ("maxScaleRel", ctypes.c_int32),
    ]


class DISPLAYCONFIG_SOURCE_DPI_SCALE_SET(ctypes.Structure):
    _fields_ = [
        ("header", DISPLAYCONFIG_DEVICE_INFO_HEADER),
        ("scaleRel", ctypes.c_int32),
    ]


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_byte * 8),
    ]


class NOTIFYICONIDENTIFIER(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("hWnd", wintypes.HWND),
        ("uID", wintypes.UINT),
        ("guidItem", GUID),
    ]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


WNDPROC = ctypes.WINFUNCTYPE(
    wintypes.LPARAM, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
)
user32.DefWindowProcW.argtypes = [
    wintypes.HWND,
    wintypes.UINT,
    wintypes.WPARAM,
    wintypes.LPARAM,
]
user32.DefWindowProcW.restype = wintypes.LPARAM

user32.SetClassLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
user32.SetClassLongPtrW.restype = ctypes.c_void_p


def _simple_wnd_proc(hwnd, msg, wp, lp):
    return user32.DefWindowProcW(hwnd, msg, wp, lp)


C_WND_PROC = WNDPROC(_simple_wnd_proc)


class WNDCLASSEXW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.UINT),
        ("style", wintypes.UINT),
        ("lpfnWndProc", WNDPROC),
        ("cbClsExtra", ctypes.c_int),
        ("cbWndExtra", ctypes.c_int),
        ("hInstance", wintypes.HINSTANCE),
        ("hIcon", wintypes.HICON),
        ("hCursor", wintypes.HICON),
        ("hbrBackground", wintypes.HBRUSH),
        ("lpszMenuName", wintypes.LPCWSTR),
        ("lpszClassName", wintypes.LPCWSTR),
        ("hIconSm", wintypes.HICON),
    ]


def init_win32_environment():
    """Ensure Per-Monitor V2 DPI awareness and access to the interactive desktop."""
    try:
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception as e:
        print(f"Warning: SetProcessDpiAwarenessContext failed: {e}")

    try:
        hDefDesk = user32.OpenDesktopW("Default", 0, False, 0x01FF)
        if hDefDesk:
            user32.SetThreadDesktop(hDefDesk)
    except Exception as e:
        print(f"Warning: SetThreadDesktop failed: {e}")


def get_primary_display_info():
    """Retrieve adapterId and sourceId for the primary display (position 0, 0)."""
    numPath = wintypes.UINT()
    numMode = wintypes.UINT()
    status = user32.GetDisplayConfigBufferSizes(
        QDC_ONLY_ACTIVE_PATHS, ctypes.byref(numPath), ctypes.byref(numMode)
    )
    if status != 0:
        raise RuntimeError(f"GetDisplayConfigBufferSizes failed: {status}")

    paths = (DISPLAYCONFIG_PATH_INFO * numPath.value)()
    modes = (DISPLAYCONFIG_MODE_INFO * numMode.value)()
    status = user32.QueryDisplayConfig(
        QDC_ONLY_ACTIVE_PATHS,
        ctypes.byref(numPath),
        paths,
        ctypes.byref(numMode),
        modes,
        None,
    )
    if status != 0:
        raise RuntimeError(f"QueryDisplayConfig failed: {status}")

    for i in range(numPath.value):
        p = paths[i]
        modeIdx = p.sourceInfo.modeInfoIdx
        if modeIdx < numMode.value:
            pos = modes[modeIdx].modeInfo.sourceMode.position
            if pos.x == 0 and pos.y == 0:
                return p.sourceInfo.adapterId, p.sourceInfo.id
    return None, None


def get_dpi(adapterId, sourceId):
    """Query current and recommended DPI percentage."""
    packet = DISPLAYCONFIG_SOURCE_DPI_SCALE_GET()
    packet.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE
    packet.header.size = ctypes.sizeof(DISPLAYCONFIG_SOURCE_DPI_SCALE_GET)
    packet.header.adapterId = adapterId
    packet.header.id = sourceId
    status = user32.DisplayConfigGetDeviceInfo(ctypes.byref(packet.header))
    if status == 0:
        minAbs = abs(packet.minScaleRel)
        curIdx = minAbs + packet.curScaleRel
        recIdx = minAbs
        curDpi = DPI_VALS[curIdx] if curIdx < len(DPI_VALS) else 100
        recDpi = DPI_VALS[recIdx] if recIdx < len(DPI_VALS) else 100
        return curDpi, packet.curScaleRel, recDpi
    return None, None, None


def set_dpi(adapterId, sourceId, targetDpi):
    """Set display scaling percentage."""
    curDpi, _, recDpi = get_dpi(adapterId, sourceId)
    if curDpi == targetDpi:
        return True
    try:
        targetIdx = DPI_VALS.index(targetDpi)
        recIdx = DPI_VALS.index(recDpi)
    except ValueError:
        raise ValueError(f"Unsupported DPI value {targetDpi}. Supported: {DPI_VALS}")

    scaleRel = targetIdx - recIdx
    packet = DISPLAYCONFIG_SOURCE_DPI_SCALE_SET()
    packet.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_DPI_SCALE
    packet.header.size = ctypes.sizeof(DISPLAYCONFIG_SOURCE_DPI_SCALE_SET)
    packet.header.adapterId = adapterId
    packet.header.id = sourceId
    packet.scaleRel = scaleRel
    status = user32.DisplayConfigSetDeviceInfo(ctypes.byref(packet.header))
    return status == 0


def get_system_theme():
    """Returns 0 for dark theme, 1 for light theme."""
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_THEMES, 0, winreg.KEY_READ) as k:
            val, _ = winreg.QueryValueEx(k, VAL_LIGHT_THEME)
            return val
    except Exception:
        return 1


def set_system_theme(val):
    """Sets AppsUseLightTheme (0 for dark, 1 for light)."""
    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER, REG_THEMES, 0, winreg.KEY_SET_VALUE
        ) as k:
            winreg.SetValueEx(k, VAL_LIGHT_THEME, 0, winreg.REG_DWORD, val)
    except Exception as e:
        print(f"Warning: Failed to update theme setting: {e}")


def capture_screen_rect(x, y, w, h):
    """Captures a specific rectangle from the desktop compositor surface."""
    hDesktopDC = user32.GetDC(None)
    hMemDC = gdi32.CreateCompatibleDC(hDesktopDC)
    hBitmap = gdi32.CreateCompatibleBitmap(hDesktopDC, w, h)
    hOld = gdi32.SelectObject(hMemDC, hBitmap)
    gdi32.BitBlt(hMemDC, 0, 0, w, h, hDesktopDC, x, y, SRCCOPY)

    bmi = BITMAPINFOHEADER()
    bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.biWidth = w
    bmi.biHeight = -h  # top-down
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0

    buf = (ctypes.c_byte * (w * h * 4))()
    gdi32.GetDIBits(hMemDC, hBitmap, 0, h, ctypes.byref(buf), ctypes.byref(bmi), 0)

    gdi32.SelectObject(hMemDC, hOld)
    gdi32.DeleteObject(hBitmap)
    gdi32.DeleteDC(hMemDC)
    user32.ReleaseDC(None, hDesktopDC)

    return Image.frombuffer("RGBA", (w, h), bytes(buf), "raw", "BGRA", 0, 1)


def generate_screenshot(output_path, target_dpi=175, theme="system", restore_dpi=True, exe_path=None):
    """Full automation workflow to generate the screenshot."""
    init_win32_environment()

    if exe_path is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.dirname(script_dir)
        exe_path = os.path.join(repo_root, "bin", "uBTAudioTray.exe")

    if not os.path.exists(exe_path):
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    # Display scaling tracking
    adapterId, sourceId = get_primary_display_info()
    if not adapterId:
        raise RuntimeError("Could not find primary display adapter and source ID.")

    orig_dpi, _, _ = get_dpi(adapterId, sourceId)
    print(f"Original primary monitor DPI: {orig_dpi}%")

    # Theme tracking
    orig_theme = get_system_theme()
    theme_changed = False
    if theme == "dark" and orig_theme != 0:
        print("Switching to Dark Theme for capture...")
        set_system_theme(0)
        theme_changed = True
    elif theme == "light" and orig_theme != 1:
        print("Switching to Light Theme for capture...")
        set_system_theme(1)
        theme_changed = True

    app_proc = None

    try:
        # Step 1: Close any existing instances
        print("Closing any running uBTAudioTray instances...")
        subprocess.run(["taskkill", "/F", "/IM", "uBTAudioTray*.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)

        # Step 2: Set display scaling
        if orig_dpi != target_dpi:
            print(f"Setting primary monitor DPI scaling to {target_dpi}%...")
            if not set_dpi(adapterId, sourceId, target_dpi):
                raise RuntimeError(f"Failed to set DPI scaling to {target_dpi}%")
            time.sleep(2.0)  # Allow shell to relayout taskbar and icons
            cur_dpi, _, _ = get_dpi(adapterId, sourceId)
            print(f"Verified DPI: {cur_dpi}%")
        else:
            print(f"Primary monitor is already at {target_dpi}% DPI.")

        # Step 3: Launch application
        print(f"Launching {exe_path}...")
        app_proc = subprocess.Popen([exe_path], cwd=os.path.dirname(exe_path))

        # Wait for hidden message window
        hwndMsg = None
        for _ in range(50):
            hwndMsg = user32.FindWindowW("uBTAudioTray_HiddenMsgWnd", "uBTAudioTrayMsg")
            if hwndMsg:
                break
            time.sleep(0.1)

        if not hwndMsg:
            raise RuntimeError("Failed to detect uBTAudioTray message window.")

        time.sleep(1.2)  # Wait for initial Bluetooth scan to complete

        # Step 4: Find tray icon position
        nid = NOTIFYICONIDENTIFIER()
        nid.cbSize = ctypes.sizeof(NOTIFYICONIDENTIFIER)
        nid.hWnd = hwndMsg
        nid.uID = 1
        rc = wintypes.RECT()
        hr = shell32.Shell_NotifyIconGetRect(ctypes.byref(nid), ctypes.byref(rc))

        if hr == 0:
            trayX = (rc.left + rc.right) // 2
            trayY = (rc.top + rc.bottom) // 2
            print(f"Detected tray icon at ({trayX}, {trayY})")
        else:
            # Fallback to bottom right area of primary screen
            trayX = 1750
            trayY = 1040
            print(f"Tray rect query returned {hex(hr)}; using fallback cursor ({trayX}, {trayY})")

        user32.SetCursorPos(trayX, trayY)
        time.sleep(0.2)

        # Step 5: Open tray menu
        print("Opening tray menu...")
        user32.PostMessageW(hwndMsg, WM_APP_TRAYMSG, 1, WM_RBUTTONUP)

        hMenu = None
        for _ in range(30):
            hMenu = user32.FindWindowW("uBTAudioTray_MenuWindow", None)
            if hMenu and user32.IsWindowVisible(hMenu):
                break
            time.sleep(0.1)

        if not hMenu or not user32.IsWindowVisible(hMenu):
            raise RuntimeError("Failed to open tray menu.")

        # Step 6: Open settings window
        menuDpi = user32.GetDpiForWindow(hMenu) if hasattr(user32, "GetDpiForWindow") else 96
        def scale(v): return (v * menuDpi) // 96

        menuClientRc = wintypes.RECT()
        user32.GetClientRect(hMenu, ctypes.byref(menuClientRc))
        menuW = menuClientRc.right - menuClientRc.left

        gearX = menuW - scale(74)
        gearY = scale(20)
        print(f"Clicking settings gear at ({gearX}, {gearY}) on menu (width={menuW}, dpi={menuDpi})...")
        lParam = (gearY << 16) | (gearX & 0xFFFF)
        user32.SendMessageW(hMenu, WM_LBUTTONDOWN, 1, lParam)
        user32.SendMessageW(hMenu, WM_LBUTTONUP, 0, lParam)

        hSettings = None
        for _ in range(30):
            hSettings = user32.FindWindowW("uBTAudioTray_SettingsWindow", None)
            if hSettings and user32.IsWindowVisible(hSettings):
                break
            time.sleep(0.1)

        if not hSettings or not user32.IsWindowVisible(hSettings):
            raise RuntimeError("Failed to open settings window.")

        # Step 7: Re-focus tray menu
        print("Re-focusing tray menu...")
        user32.SetForegroundWindow(hMenu)
        time.sleep(0.4)

        # Step 8: Calculate capture bounds
        rcMenu = wintypes.RECT()
        rcSettings = wintypes.RECT()
        user32.GetWindowRect(hMenu, ctypes.byref(rcMenu))
        user32.GetWindowRect(hSettings, ctypes.byref(rcSettings))

        shadowPad = scale(45)
        capLeft = min(rcMenu.left, rcSettings.left) - shadowPad
        capTop = min(rcMenu.top, rcSettings.top) - shadowPad
        capRight = max(rcMenu.right, rcSettings.right) + shadowPad
        capBottom = max(rcMenu.bottom, rcSettings.bottom) + shadowPad
        capW = capRight - capLeft
        capH = capBottom - capTop
        print(f"Capture region: ({capLeft}, {capTop}, {capW}x{capH})")

        # Step 9: Two-pass backdrop capture for shadow & alpha matting
        hBrushWhite = gdi32.CreateSolidBrush(0x00FFFFFF)
        hBrushBlack = gdi32.CreateSolidBrush(0x00000000)

        hInstance = kernel32.GetModuleHandleW(None)
        clsName = f"BTTrayBackdrop_{os.getpid()}"

        wndClass = WNDCLASSEXW()
        wndClass.cbSize = ctypes.sizeof(WNDCLASSEXW)
        wndClass.lpfnWndProc = C_WND_PROC
        wndClass.hInstance = hInstance
        wndClass.lpszClassName = clsName
        wndClass.hbrBackground = hBrushWhite
        user32.RegisterClassExW(ctypes.byref(wndClass))

        hBackdrop = user32.CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            clsName,
            "Backdrop",
            WS_POPUP,
            capLeft - 20,
            capTop - 20,
            capW + 40,
            capH + 40,
            None,
            None,
            hInstance,
            None,
        )

        # Ensure correct Z-stack: Backdrop (bottom) -> Settings -> Menu (top)
        user32.SetWindowPos(
            hBackdrop,
            HWND_TOPMOST,
            capLeft - 20,
            capTop - 20,
            capW + 40,
            capH + 40,
            SWP_NOACTIVATE | SWP_SHOWWINDOW,
        )
        user32.SetWindowPos(
            hSettings, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        )
        user32.SetWindowPos(
            hMenu, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        )
        user32.SetForegroundWindow(hMenu)

        # Pass 1: Capture on White
        dwmapi.DwmFlush()
        time.sleep(0.4)
        img_white = capture_screen_rect(capLeft, capTop, capW, capH)

        # Pass 2: Switch to Black and Capture
        user32.SetClassLongPtrW(hBackdrop, GCLP_HBRBACKGROUND, hBrushBlack)
        user32.InvalidateRect(hBackdrop, None, True)
        user32.UpdateWindow(hBackdrop)
        dwmapi.DwmFlush()
        time.sleep(0.4)
        img_black = capture_screen_rect(capLeft, capTop, capW, capH)

        # Destroy backdrop
        user32.DestroyWindow(hBackdrop)
        gdi32.DeleteObject(hBrushWhite)
        gdi32.DeleteObject(hBrushBlack)

        # Step 10: Compute Alpha Matting
        print("Computing exact transparency & drop shadow alpha matting...")
        w_data = img_white.load()
        b_data = img_black.load()
        out = Image.new("RGBA", (capW, capH), (0, 0, 0, 0))
        out_data = out.load()

        for y in range(capH):
            for x in range(capW):
                rw, gw, bw, _ = w_data[x, y]
                rb, gb, bb, _ = b_data[x, y]
                ar = 255 - (rw - rb)
                ag = 255 - (gw - gb)
                ab = 255 - (bw - bb)
                a = max(0, min(255, int(round((ar + ag + ab) / 3.0))))

                if a <= 2:
                    out_data[x, y] = (0, 0, 0, 0)
                elif a >= 253:
                    out_data[x, y] = (rb, gb, bb, 255)
                else:
                    fa = a / 255.0
                    r = min(255, max(0, int(round(rb / fa))))
                    g = min(255, max(0, int(round(gb / fa))))
                    b = min(255, max(0, int(round(bb / fa))))
                    out_data[x, y] = (r, g, b, a)

        # Crop excess outer transparent space
        bbox = out.getbbox()
        if bbox:
            crop_box = (
                max(0, bbox[0] - 10),
                max(0, bbox[1] - 10),
                min(capW, bbox[2] + 10),
                min(capH, bbox[3] + 10),
            )
            out = out.crop(crop_box)

        # Ensure output directory exists and save
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        out.save(output_path, "PNG")
        print(f"\n[SUCCESS] Screenshot saved: {output_path} ({out.size[0]}x{out.size[1]})")

    finally:
        # Cleanup application windows
        try:
            hMenu = user32.FindWindowW("uBTAudioTray_MenuWindow", None)
            if hMenu:
                user32.SendMessageW(hMenu, WM_CLOSE, 0, 0)
        except Exception:
            pass

        if app_proc:
            try:
                app_proc.terminate()
            except Exception:
                pass

        # Restore Theme
        if theme_changed:
            print(f"Restoring system theme to {orig_theme}...")
            set_system_theme(orig_theme)

        # Restore DPI
        if restore_dpi and orig_dpi != target_dpi:
            print(f"Restoring primary monitor DPI scaling to {orig_dpi}%...")
            set_dpi(adapterId, sourceId, orig_dpi)
            time.sleep(1.0)
            restored_dpi, _, _ = get_dpi(adapterId, sourceId)
            print(f"Primary monitor DPI restored to: {restored_dpi}%")


def main():
    parser = argparse.ArgumentParser(
        description="Generate updated uBTAudioTray screenshot at 175% DPI with transparent window shadows."
    )
    parser.add_argument(
        "-o",
        "--output",
        default="res/Screenshot_uBTAudioTray.png",
        help="Destination path for the screenshot (default: res/Screenshot_uBTAudioTray.png)",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=175,
        help="Display scaling percentage to apply (default: 175)",
    )
    parser.add_argument(
        "--theme",
        choices=["system", "dark", "light"],
        default="system",
        help="Window theme to capture (default: system)",
    )
    parser.add_argument(
        "--keep-scale",
        action="store_true",
        help="Do not restore original display scaling after capture",
    )
    parser.add_argument(
        "--exe",
        default=None,
        help="Path to uBTAudioTray.exe (default: auto-detect in bin/)",
    )

    args = parser.parse_args()

    generate_screenshot(
        output_path=args.output,
        target_dpi=args.dpi,
        theme=args.theme,
        restore_dpi=not args.keep_scale,
        exe_path=args.exe,
    )


if __name__ == "__main__":
    main()
