#pragma once

#include <windows.h>
#include <stdbool.h>

#define MAX_SELECTED_DEVICES 32
#define MAC_ADDR_LEN 24

typedef struct {
    wchar_t selectedAddresses[MAX_SELECTED_DEVICES][MAC_ADDR_LEN];
    int selectedCount;

    bool enableNotifications;
    bool sendMediaPlayOnConnect;
    bool sendMediaPauseOnDisconnect;
    bool useUiaConnect;
    bool useUiaDisconnect;
    bool useHciDisconnect;
} AppState;

void app_state_init_default(AppState* state);
bool app_state_load(AppState* state);
bool app_state_save(const AppState* state);

bool app_state_is_selected(const AppState* state, const wchar_t* address);
void app_state_set_selected(AppState* state, const wchar_t* address, bool selected);
void app_state_prune_unseen(AppState* state, const wchar_t activeAddresses[][MAC_ADDR_LEN], int activeCount);

