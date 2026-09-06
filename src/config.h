#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

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
