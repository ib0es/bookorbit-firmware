#pragma once

#include <Arduino.h>
#include "../../app/app_state.h"

void drawSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSettingsItem(AppContext& ctx, int index);

void drawWifiSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateWifiSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateWifiSettingsItem(AppContext& ctx, int index);

void drawServerSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateServerSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateServerSettingsItem(AppContext& ctx, int index);

void drawSyncSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSyncSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSyncSettingsItem(AppContext& ctx, int index);

void drawSleepCoverSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSleepCoverSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSleepCoverSettingsItem(AppContext& ctx, int index);

void drawSdSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSdSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);

void drawLanguageSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateLanguageSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateLanguageSettingsItem(AppContext& ctx, int index);

void drawFrontlightScreen(AppContext& ctx, bool fullRefresh = false);
// Value (0..100, 5 % steps) for a tap at x on slider `row` of the last frontlight screen.
bool frontlightTrackValue(int row, int16_t x, int& valueOut);
