#pragma once

#include <Arduino.h>

void drawStatusModal(const char* title, const char* line1, const char* line2 = nullptr, const char* line3 = nullptr, const char* line4 = nullptr, const char* footerHint = nullptr, bool fullRefresh = false);

// Hit actions of the dialog buttons (touch boards).
enum ModalButtonAction : int { MODAL_HIT_OK = 1, MODAL_HIT_CANCEL = 2 };

// Confirmation dialog. On touch boards the footer is two buttons, [cancel]
// and [ok], registered as MODAL_HIT_CANCEL / MODAL_HIT_OK; on button boards
// it is `footerHint`.
void drawConfirmModal(const char* title, const char* line1, const char* line2, const char* line3,
                      const char* footerHint, const char* okLabel, const char* cancelLabel);
