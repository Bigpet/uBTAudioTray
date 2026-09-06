#include "ui_common.h"
#include <math.h>

TaskbarEdge ui_get_taskbar_edge(POINT pt, RECT* outWorkArea) {
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { 0 };
    mi.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfoW(hMon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, outWorkArea, 0);
        return TASKBAR_EDGE_BOTTOM;
    }

    if (outWorkArea) {
        *outWorkArea = mi.rcWork;
    }

    if (mi.rcWork.left > mi.rcMonitor.left) return TASKBAR_EDGE_LEFT;
    if (mi.rcWork.top > mi.rcMonitor.top) return TASKBAR_EDGE_TOP;
    if (mi.rcWork.right < mi.rcMonitor.right) return TASKBAR_EDGE_RIGHT;
    if (mi.rcWork.bottom < mi.rcMonitor.bottom) return TASKBAR_EDGE_BOTTOM;
    return TASKBAR_EDGE_BOTTOM;
}

UINT ui_get_window_dpi(HWND hwnd) {
    typedef UINT (WINAPI *FnGetDpiForWindow)(HWND);
    static FnGetDpiForWindow pfnGetDpiForWindow = NULL;
    static bool resolved = false;

    if (!resolved) {
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        if (hUser) {
            pfnGetDpiForWindow = (FnGetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
        }
        resolved = true;
    }

    if (pfnGetDpiForWindow && hwnd && IsWindow(hwnd)) {
        UINT dpi = pfnGetDpiForWindow(hwnd);
        if (dpi > 0) return dpi;
    }

    HDC hdc = GetDC(hwnd);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(hwnd, hdc);
        if (dpi > 0) return (UINT)dpi;
    }

    return 96;
}

UINT ui_get_point_dpi(POINT pt) {
    typedef HRESULT (WINAPI *FnGetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    static FnGetDpiForMonitor pfnGetDpiForMonitor = NULL;
    static bool resolved = false;

    if (!resolved) {
        HMODULE hShcore = LoadLibraryW(L"shcore.dll");
        if (hShcore) {
            pfnGetDpiForMonitor = (FnGetDpiForMonitor)GetProcAddress(hShcore, "GetDpiForMonitor");
        }
        resolved = true;
    }

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (pfnGetDpiForMonitor && hMon) {
        UINT dpiX = 96, dpiY = 96;
        if (SUCCEEDED(pfnGetDpiForMonitor(hMon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiX > 0) {
            return dpiX;
        }
    }

    HDC hdc = GetDC(NULL);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        if (dpi > 0) return (UINT)dpi;
    }

    return 96;
}

int ui_scale(int value, UINT dpi) {
    if (dpi == 0 || dpi == 96) return value;
    return MulDiv(value, (int)dpi, 96);
}

int ui_get_system_metric_for_dpi(int metric, UINT dpi) {
    typedef int (WINAPI *FnGetSystemMetricsForDpi)(int, UINT);
    static FnGetSystemMetricsForDpi pfnGetSystemMetricsForDpi = NULL;
    static bool resolved = false;

    if (!resolved) {
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        if (hUser) {
            pfnGetSystemMetricsForDpi = (FnGetSystemMetricsForDpi)GetProcAddress(hUser, "GetSystemMetricsForDpi");
        }
        resolved = true;
    }

    if (pfnGetSystemMetricsForDpi && dpi > 0) {
        return pfnGetSystemMetricsForDpi(metric, dpi);
    }

    return ui_scale(GetSystemMetrics(metric), dpi);
}

void ui_position_window(HWND hwnd, int anchorX, int anchorY, int width, int height) {
    POINT pt = { anchorX, anchorY };
    RECT workArea = { 0 };
    TaskbarEdge edge = ui_get_taskbar_edge(pt, &workArea);
    UINT dpi = ui_get_point_dpi(pt);
    int margin = ui_scale(8, dpi);
    int clampPad = ui_scale(6, dpi);

    int left = 0;
    int top = 0;

    switch (edge) {
        case TASKBAR_EDGE_TOP:
            left = anchorX - (width / 2);
            top = anchorY + margin;
            break;
        case TASKBAR_EDGE_LEFT:
            left = anchorX + margin;
            top = anchorY - (height / 2);
            break;
        case TASKBAR_EDGE_RIGHT:
            left = anchorX - width - margin;
            top = anchorY - (height / 2);
            break;
        case TASKBAR_EDGE_BOTTOM:
        case TASKBAR_EDGE_UNKNOWN:
        default:
            left = anchorX - (width / 2);
            top = anchorY - height - margin;
            break;
    }

    // Clamp to monitor work area
    if (left < workArea.left + clampPad) left = workArea.left + clampPad;
    if (left + width > workArea.right - clampPad) left = workArea.right - width - clampPad;
    if (top < workArea.top + clampPad) top = workArea.top + clampPad;
    if (top + height > workArea.bottom - clampPad) top = workArea.bottom - height - clampPad;

    SetWindowPos(hwnd, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);
}

void ui_enable_rounded_corners(HWND hwnd) {
    DWORD preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

HFONT ui_get_font_for_dpi(int baseHeight, bool bold, UINT dpi) {
    int scaledHeight = (baseHeight < 0)
        ? -ui_scale(-baseHeight, dpi)
        : ui_scale(baseHeight, dpi);

    return CreateFontW(
        scaledHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

HFONT ui_get_font(int height, bool bold) {
    return ui_get_font_for_dpi(height, bold, 96);
}

void ui_draw_rounded_rect(HDC hdc, const RECT* rc, int radius, COLORREF fillCol, COLORREF borderCol) {
    HBRUSH hBrush = CreateSolidBrush(fillCol);
    HPEN hPen = (borderCol != CLR_INVALID) ? CreatePen(PS_SOLID, 1, borderCol) : (HPEN)GetStockObject(NULL_PEN);

    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);

    DeleteObject(hBrush);
    if (borderCol != CLR_INVALID) DeleteObject(hPen);
}

// GDI+ Flat C API declarations
typedef struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef void* GpGraphics;
typedef void* GpPen;
typedef void* GpBrush;
typedef void* GpSolidFill;

typedef struct {
    float X;
    float Y;
} GpPointF;

int __stdcall GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input, void *output);
void __stdcall GdiplusShutdown(ULONG_PTR token);

int __stdcall GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
int __stdcall GdipDeleteGraphics(GpGraphics *graphics);
int __stdcall GdipSetSmoothingMode(GpGraphics *graphics, int mode);
int __stdcall GdipSetPixelOffsetMode(GpGraphics *graphics, int mode);
int __stdcall GdipCreatePen1(DWORD color, float width, int unit, GpPen **pen);
int __stdcall GdipDeletePen(GpPen *pen);
int __stdcall GdipSetPenStartCap(GpPen *pen, int startCap);
int __stdcall GdipSetPenEndCap(GpPen *pen, int endCap);
int __stdcall GdipSetPenLineJoin(GpPen *pen, int lineJoin);
int __stdcall GdipCreateSolidFill(DWORD color, GpSolidFill **brush);
int __stdcall GdipDeleteBrush(GpBrush *brush);
int __stdcall GdipDrawLines(GpGraphics *graphics, GpPen *pen, const GpPointF *points, int count);
int __stdcall GdipFillEllipse(GpGraphics *graphics, GpBrush *brush, float x, float y, float width, float height);
int __stdcall GdipDrawEllipse(GpGraphics *graphics, GpPen *pen, float x, float y, float width, float height);

#define GDIP_SMOOTHING_ANTIALIAS 4
#define GDIP_PIXELOFFSET_HIGHQUALITY 2
#define GDIP_UNIT_PIXEL 2
#define GDIP_LINECAP_ROUND 2
#define GDIP_LINEJOIN_ROUND 2
#define GDIP_ARGB(c) (((DWORD)255 << 24) | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | ((DWORD)GetBValue(c)))

static ULONG_PTR s_gdiplusToken = 0;
static bool s_gdiplusInitialized = false;

void ui_common_init(void) {
    if (!s_gdiplusInitialized) {
        GdiplusStartupInput input = { 1, NULL, FALSE, FALSE };
        if (GdiplusStartup(&s_gdiplusToken, &input, NULL) == 0) {
            s_gdiplusInitialized = true;
        }
    }
}

void ui_common_cleanup(void) {
    if (s_gdiplusInitialized) {
        GdiplusShutdown(s_gdiplusToken);
        s_gdiplusToken = 0;
        s_gdiplusInitialized = false;
    }
}

static void ensure_gdiplus(void) {
    if (!s_gdiplusInitialized) {
        ui_common_init();
    }
}

void ui_draw_checkbox(HDC hdc, int x, int y, int size, bool isChecked, const ThemeColors* theme) {
    int radius = (size >= 20) ? 4 : 3;
    RECT rc = { x, y, x + size, y + size };
    ui_draw_rounded_rect(hdc, &rc, radius, theme->checkBg, theme->checkBorder);

    if (isChecked) {
        bool drawn = false;
        ensure_gdiplus();
        if (s_gdiplusInitialized) {
            GpGraphics* graphics = NULL;
            if (GdipCreateFromHDC(hdc, &graphics) == 0 && graphics) {
                GdipSetSmoothingMode(graphics, GDIP_SMOOTHING_ANTIALIAS);
                GdipSetPixelOffsetMode(graphics, GDIP_PIXELOFFSET_HIGHQUALITY);

                float penW = (size >= 24) ? 2.8f : ((size >= 18) ? 2.2f : 1.8f);
                GpPen* pen = NULL;
                if (GdipCreatePen1(GDIP_ARGB(theme->checkMark), penW, GDIP_UNIT_PIXEL, &pen) == 0 && pen) {
                    GdipSetPenStartCap(pen, GDIP_LINECAP_ROUND);
                    GdipSetPenEndCap(pen, GDIP_LINECAP_ROUND);
                    GdipSetPenLineJoin(pen, GDIP_LINEJOIN_ROUND);

                    // Checkmark geometry scaled to size
                    GpPointF pts[3];
                    pts[0].X = (float)x + (float)size * (3.0f / 14.0f);
                    pts[0].Y = (float)y + (float)size * 0.5f;
                    pts[1].X = (float)x + (float)size * (5.0f / 14.0f);
                    pts[1].Y = (float)y + (float)size - (float)size * (4.0f / 14.0f);
                    pts[2].X = (float)x + (float)size - (float)size * (3.0f / 14.0f);
                    pts[2].Y = (float)y + (float)size * (3.0f / 14.0f);

                    GdipDrawLines(graphics, pen, pts, 3);
                    GdipDeletePen(pen);
                    drawn = true;
                }
                GdipDeleteGraphics(graphics);
            }
        }

        if (!drawn) {
            int penW = (size >= 24) ? 3 : (size >= 18 ? 2 : 2);
            HPEN hPen = CreatePen(PS_SOLID, penW, theme->checkMark);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

            int leftX = x + MulDiv(size, 3, 14);
            int leftY = y + (size / 2);
            int midX  = x + MulDiv(size, 5, 14);
            int midY  = y + size - MulDiv(size, 4, 14);
            int rightX = x + size - MulDiv(size, 3, 14);
            int rightY = y + MulDiv(size, 3, 14);

            MoveToEx(hdc, leftX, leftY, NULL);
            LineTo(hdc, midX, midY);
            LineTo(hdc, rightX, rightY);

            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
        }
    }
}

void ui_draw_radio(HDC hdc, int x, int y, int size, bool isChecked, const ThemeColors* theme) {
    bool drawn = false;
    ensure_gdiplus();
    if (s_gdiplusInitialized) {
        GpGraphics* graphics = NULL;
        if (GdipCreateFromHDC(hdc, &graphics) == 0 && graphics) {
            GdipSetSmoothingMode(graphics, GDIP_SMOOTHING_ANTIALIAS);
            GdipSetPixelOffsetMode(graphics, GDIP_PIXELOFFSET_HIGHQUALITY);

            float penW = (size >= 24) ? 1.5f : 1.0f;
            float halfPen = penW * 0.5f;
            float circleX = (float)x + halfPen;
            float circleY = (float)y + halfPen;
            float circleSize = (float)size - penW;

            GpSolidFill* bgBrush = NULL;
            if (GdipCreateSolidFill(GDIP_ARGB(theme->checkBg), &bgBrush) == 0 && bgBrush) {
                GdipFillEllipse(graphics, bgBrush, circleX, circleY, circleSize, circleSize);
                GdipDeleteBrush(bgBrush);
            }

            GpPen* borderPen = NULL;
            if (GdipCreatePen1(GDIP_ARGB(theme->checkBorder), penW, GDIP_UNIT_PIXEL, &borderPen) == 0 && borderPen) {
                GdipDrawEllipse(graphics, borderPen, circleX, circleY, circleSize, circleSize);
                GdipDeletePen(borderPen);
            }

            if (isChecked) {
                float pad = (float)size * 0.25f;
                if (pad < 2.5f) pad = 2.5f;
                float dotX = (float)x + pad;
                float dotY = (float)y + pad;
                float dotSize = (float)size - 2.0f * pad;

                GpSolidFill* dotBrush = NULL;
                if (GdipCreateSolidFill(GDIP_ARGB(theme->checkMark), &dotBrush) == 0 && dotBrush) {
                    GdipFillEllipse(graphics, dotBrush, dotX, dotY, dotSize, dotSize);
                    GdipDeleteBrush(dotBrush);
                }
            }

            GdipDeleteGraphics(graphics);
            drawn = true;
        }
    }

    if (!drawn) {
        HBRUSH hBrush = CreateSolidBrush(theme->checkBg);
        int penW = (size >= 24) ? 2 : 1;
        HPEN hPen = CreatePen(PS_SOLID, penW, theme->checkBorder);

        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        Ellipse(hdc, x, y, x + size, y + size);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        if (isChecked) {
            int pad = size / 4;
            if (pad < 2) pad = 2;
            HBRUSH hDotBrush = CreateSolidBrush(theme->checkMark);
            HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);

            hOldBrush = (HBRUSH)SelectObject(hdc, hDotBrush);
            hOldPen = (HPEN)SelectObject(hdc, hNullPen);

            Ellipse(hdc, x + pad, y + pad, x + size - pad, y + size - pad);

            SelectObject(hdc, hOldBrush);
            SelectObject(hdc, hOldPen);
            DeleteObject(hDotBrush);
        }
    }
}

void ui_draw_status_indicator(HDC hdc, int x, int y, int size, bool isConnected, const ThemeColors* theme) {
    COLORREF col = isConnected ? theme->statusConnected : theme->statusDisconnected;
    COLORREF borderCol = isConnected
        ? (theme->isDark ? RGB(35, 160, 90) : RGB(30, 140, 75))
        : (theme->isDark ? RGB(90, 90, 90) : RGB(180, 180, 180));

    bool drawn = false;
    ensure_gdiplus();
    if (s_gdiplusInitialized) {
        GpGraphics* graphics = NULL;
        if (GdipCreateFromHDC(hdc, &graphics) == 0 && graphics) {
            GdipSetSmoothingMode(graphics, GDIP_SMOOTHING_ANTIALIAS);
            GdipSetPixelOffsetMode(graphics, GDIP_PIXELOFFSET_HIGHQUALITY);

            float penW = (size >= 24) ? 1.5f : 1.0f;
            float halfPen = penW * 0.5f;
            float circleX = (float)x + halfPen;
            float circleY = (float)y + halfPen;
            float circleSize = (float)size - penW;

            GpSolidFill* fillBrush = NULL;
            if (GdipCreateSolidFill(GDIP_ARGB(col), &fillBrush) == 0 && fillBrush) {
                GdipFillEllipse(graphics, fillBrush, circleX, circleY, circleSize, circleSize);
                GdipDeleteBrush(fillBrush);
            }

            GpPen* borderPen = NULL;
            if (GdipCreatePen1(GDIP_ARGB(borderCol), penW, GDIP_UNIT_PIXEL, &borderPen) == 0 && borderPen) {
                GdipDrawEllipse(graphics, borderPen, circleX, circleY, circleSize, circleSize);
                GdipDeletePen(borderPen);
            }

            GdipDeleteGraphics(graphics);
            drawn = true;
        }
    }

    if (!drawn) {
        HBRUSH hBrush = CreateSolidBrush(col);
        int penW = (size >= 24) ? 2 : 1;
        HPEN hPen = CreatePen(PS_SOLID, penW, borderCol);

        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        Ellipse(hdc, x, y, x + size, y + size);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);
    }
}


