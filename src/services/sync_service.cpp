#include "sync_service.h"
#include "sync_internal.h"
#include "../platform/hal/mem_stats.h"
#include "../net/bookorbit_client.h"
#include "../core/book_formats.h"
#include "../platform/hal/buttons.h"
#include "../platform/hal/storage.h"
#include "../ui/i18n.h"
#include "../core/ui_port.h"
#include "../reader/reader.h"
#include "../core/progress_sync.h"
#include "../convert/sd_convert.h"
#include "bookmarks.h"
#include "sync_clock.h"
#include "../ui/text_utils.h"
#include "power_service.h"
#include "wifi_session.h"
#include "../storage/catalog_store.h"
#include "../ui/wifi_messages.h"

bool SyncService::s_syncCancelled = false;
uint32_t SyncService::s_lastProgressTime = 0;
int SyncService::s_lastProgressPercent = -1;
String SyncService::s_downloadingBookTitle = "";

// BACK held now, or a BACK click that arrived since the last poll, cancels the
// running network operation. Other pending clicks are dropped (screens are busy).
bool SyncService::checkSyncCancel() {
    if (s_syncCancelled) return true;
    Buttons.update();
    bool back = Buttons.isPressed(BTN_BACK);
    ButtonEvent evt;
    // A tap cancels too: touch-only boards (Picco) have no BACK key.
    while (!back && Buttons.getEvent(evt)) back = (evt.id == BTN_BACK || evt.id == BTN_TAP);
    if (back) {
        Buttons.clearEvents();
        s_syncCancelled = true;
    }
    return back;
}

void SyncService::abortSync(AppContext& ctx, AppState returnState, const char* title) {
    Serial.println("[Sync] Operation aborted by user.");
    BookOrbit.disconnectWiFi();
    Ui::status(title ? title : tr(MsgId::SYNC_ABORTED_TITLE), tr(MsgId::SYNC_ABORTED_USER), tr(MsgId::SYNC_ABORTED_BY_USER_L2), tr(MsgId::SYNC_ABORT_DISCONNECTING), tr(MsgId::SYNC_ABORT_RETURNING));
    delay(1200);
    Buttons.clearEvents();
    s_syncCancelled = false;
    showState(ctx, returnState);
}

// Goes back to `state` after a sync step and redraws it (reading -> the open page).
void SyncService::showState(AppContext& ctx, AppState state) {
    if (state == STATE_READING || state == STATE_READER_MENU) {
        state = Reader.isOpened() ? STATE_READING : STATE_HOME;
    }
    ctx.currentState = state;
    if (Reader.isOpened()) placeBookmarks(Bookmarks);   // ones that came from the server
    Ui::redrawScreen(false);
}

void SyncService::reportDownloadProgress(int percent) {
    uint32_t now = millis();
    // An e-ink refresh blocks ~0.45 s, the download with it: at most every 5 s
    // (every 2 s it took a fifth of a 10 s download on the X4).
    if (now - s_lastProgressTime >= 5000 && percent > s_lastProgressPercent && percent < 100) {
        s_lastProgressPercent = percent;
        s_lastProgressTime = now;

        char pctText[48];
        snprintf(pctText, sizeof(pctText), tr(MsgId::SYNC_DOWNLOADING_FMT), percent);

        char bar[32];
        int filled = (percent * 14) / 100;
        int p = 0;
        bar[p++] = '[';
        for (int i = 0; i < 14; i++) {
            bar[p++] = (i < filled) ? '=' : (i == filled ? '>' : ' ');
        }
        bar[p++] = ']';
        bar[p] = '\0';

        Ui::status(tr(MsgId::LIB_HINT_DOWNLOAD_BOOK), s_downloadingBookTitle.c_str(), pctText, bar, nullptr, tr(MsgId::COMMON_CANCEL));
    }
}
