#pragma once

#include <Arduino.h>
#include "../../app/app_state.h"

void drawSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSettingsItem(AppContext& ctx, int index);

void drawWifiSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateWifiSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateWifiSettingsItem(AppContext& ctx, int index);

void drawWebSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateWebSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);

void drawServerSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateServerSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateServerSettingsItem(AppContext& ctx, int index);

void drawSyncSettingsScreen(AppContext& ctx, bool fullRefresh = false);
// Settings -> Reading: status bar, full refresh, pictures, UP/DOWN keys.
void drawReadingSettingsScreen(AppContext& ctx, bool fullRefresh = false);
// Settings -> Reading -> Status bar: what the lines above and below the text show.
void drawStatusBarScreen(AppContext& ctx, bool fullRefresh = false);
void updateSyncSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSyncSettingsItem(AppContext& ctx, int index);

void drawSleepSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSleepSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSleepSettingsItem(AppContext& ctx, int index);

void drawSleepCoverSettingsScreen(AppContext& ctx, bool fullRefresh = false);
// Settings -> Screen orientation: Auto (with a motion sensor) and the four turns.
void drawOrientationSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateOrientationSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
// A setting with a few values as a list, the one in use checked (Choices, ctx.choiceKind).
void drawChoiceScreen(AppContext& ctx, bool fullRefresh = false);
void updateChoiceSelection(AppContext& ctx, int prevIndex, int newIndex);
// Status bar -> Top / Bottom: the items of the line, as switches, a preview below.
void drawSbTopScreen(AppContext& ctx, bool fullRefresh = false);
void updateSbTopSelection(AppContext& ctx, int prevIndex, int newIndex);
int sbTopItemAt(int index);
void drawSbBottomScreen(AppContext& ctx, bool fullRefresh = false);
void updateSbBottomSelection(AppContext& ctx, int prevIndex, int newIndex);
// The SbBottomItem bit of row `index` of that screen.
int sbBottomItemAt(int index);
void updateSleepCoverSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateSleepCoverSettingsItem(AppContext& ctx, int index);

void drawSdSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateSdSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);

void drawUsbMscSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateUsbMscSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateUsbMscSettingsItem(AppContext& ctx, int index);
void drawUsbMscScreen(const AppContext& ctx, bool fullRefresh = false);

void drawLanguageSettingsScreen(AppContext& ctx, bool fullRefresh = false);
void updateLanguageSettingsSelection(AppContext& ctx, int prevIndex, int newIndex);
void updateLanguageSettingsItem(AppContext& ctx, int index);
void drawKeyboardSettingsScreen(AppContext& ctx, bool fullRefresh = false);

// Settings -> Frontlight: sliders, Turn on / Turn off.
void drawFrontlightScreen(AppContext& ctx, bool fullRefresh = false);
// The same as a panel pulled down from the top edge over any screen (a modal).
void drawFrontlightPanel(const AppContext& ctx, int selected);
// Value (0..100, 5 % steps) for a tap at x on slider `row` of the last frontlight screen.
bool frontlightTrackValue(int row, int16_t x, int& valueOut);
