#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// ============================================================================
// Application Information
// ============================================================================
#ifndef APP_VERSION_STR
#define APP_VERSION_STR "1.0.2"
#endif

#ifndef APP_VERSION_WIDE
#define APP_VERSION_WIDE L"1.0.2"
#endif

// ============================================================================
// Feature Toggles (1 = enabled, 0 = disabled)
// ============================================================================
#ifndef ENABLE_KS
#define ENABLE_KS 1
#endif

#ifndef ENABLE_API_HCI
#define ENABLE_API_HCI 1
#endif

#ifndef ENABLE_UI
#define ENABLE_UI 1
#endif

#if !ENABLE_KS && !ENABLE_API_HCI && !ENABLE_UI
#error "At least one connection method (ENABLE_KS, ENABLE_API_HCI, or ENABLE_UI) must be enabled."
#endif

// ============================================================================
// Tray & Application Timing (in milliseconds)
// ============================================================================
#ifndef TRAY_BUSY_BLINK_INTERVAL_MS
#define TRAY_BUSY_BLINK_INTERVAL_MS         250
#endif

#ifndef TRAY_NOTIFICATION_TIMEOUT_MS
#define TRAY_NOTIFICATION_TIMEOUT_MS        3000
#endif

#ifndef ENDPOINT_DEBOUNCE_SCAN_MS
#define ENDPOINT_DEBOUNCE_SCAN_MS           150
#endif

#ifndef MEDIA_CONTROL_DELAY_MS
#define MEDIA_CONTROL_DELAY_MS              500
#endif

#ifndef SETTLED_POLL_INTERVAL_MS
#define SETTLED_POLL_INTERVAL_MS            350
#endif

#ifndef SETTLED_POLL_MAX_ATTEMPTS
#define SETTLED_POLL_MAX_ATTEMPTS           8
#endif

#ifndef UI_SPINNER_TIMER_MS
#define UI_SPINNER_TIMER_MS                 100
#endif

// ============================================================================
// Bluetooth & Kernel Streaming (KS) Timing
// ============================================================================
#ifndef BT_SERVICE_CACHE_TTL_MS
#define BT_SERVICE_CACHE_TTL_MS             45000
#endif

#ifndef KS_CONNECT_VERIFY_POLL_MS
#define KS_CONNECT_VERIFY_POLL_MS           150
#endif

#ifndef KS_CONNECT_VERIFY_MAX_POLLS
#define KS_CONNECT_VERIFY_MAX_POLLS         24
#endif

#ifndef KS_DISCONNECT_VERIFY_POLL_MS
#define KS_DISCONNECT_VERIFY_POLL_MS        150
#endif

#ifndef KS_DISCONNECT_VERIFY_MAX_POLLS
#define KS_DISCONNECT_VERIFY_MAX_POLLS       8
#endif

// ============================================================================
// UI Automation Timing
// ============================================================================
#ifndef UIA_SETTINGS_WINDOW_TIMEOUT_MS
#define UIA_SETTINGS_WINDOW_TIMEOUT_MS      8000
#endif

#ifndef UIA_BUTTON_READY_TIMEOUT_MS
#define UIA_BUTTON_READY_TIMEOUT_MS         6000
#endif

#ifndef UIA_FIND_WINDOW_POLL_MS
#define UIA_FIND_WINDOW_POLL_MS             250
#endif

#ifndef UIA_INITIAL_SETTLE_DELAY_MS
#define UIA_INITIAL_SETTLE_DELAY_MS         200
#endif

#ifndef UIA_READY_POLL_INTERVAL_MS
#define UIA_READY_POLL_INTERVAL_MS          200
#endif

#ifndef UIA_POST_CLICK_CONFIRM_TIMEOUT_MS
#define UIA_POST_CLICK_CONFIRM_TIMEOUT_MS   900
#endif

#ifndef UIA_POST_CLICK_CONFIRM_POLL_MS
#define UIA_POST_CLICK_CONFIRM_POLL_MS      150
#endif

#ifndef UIA_LANDMARK_TIMEOUT_MS
#define UIA_LANDMARK_TIMEOUT_MS             2000
#endif

#ifndef UIA_SCROLL_PAGEDOWN_DELAY_MS
#define UIA_SCROLL_PAGEDOWN_DELAY_MS        600
#endif

#ifndef UIA_CLOSE_DELAY_CONFIRMED_MS
#define UIA_CLOSE_DELAY_CONFIRMED_MS        180
#endif

#ifndef UIA_CLOSE_DELAY_UNCONFIRMED_MS
#define UIA_CLOSE_DELAY_UNCONFIRMED_MS      120
#endif

