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

using namespace SyncInternal;

// ---- background upload after closing a book ----
namespace {
enum class BgState { Idle, Connecting };
BgState s_bgState = BgState::Idle;
int s_bgFileId = 0;
bool s_bgOnlyIfDirty = true;
String s_bgPath;
uint32_t s_bgStartedAt = 0;
constexpr uint32_t kBgConnectTimeoutMs = 15000;
}  // namespace

bool SyncService::backgroundActive() { return s_bgState != BgState::Idle; }

void SyncService::cancelBackground() {
    if (s_bgState == BgState::Idle) return;
    s_bgState = BgState::Idle;
    BookOrbit.setForegroundConnectHook(nullptr);
    Serial.println("[Sync] Background upload cancelled; progress stays queued (dirty).");
    // The foreground operation that cancelled us reconnects as it needs.
}

void SyncService::startBackgroundUpload(AppContext& ctx, int fileId, const String& filePath, bool onlyIfDirty) {
    WifiSession::prepare();
    if (ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0) return;
    if (s_bgState != BgState::Idle) cancelBackground();
    s_bgFileId = fileId;
    s_bgPath = filePath;
    s_bgOnlyIfDirty = onlyIfDirty;
    s_bgStartedAt = millis();
    s_bgState = BgState::Connecting;
    BookOrbit.setForegroundConnectHook(&SyncService::cancelBackground);
    BookOrbit.beginWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str());
    Serial.printf("[Sync] Background %s queued for %s\n", onlyIfDirty ? "upload" : "sync", filePath.c_str());
}

void SyncService::tickBackground(AppContext& ctx) {
    if (s_bgState != BgState::Connecting) return;
    if (!BookOrbit.isConnected()) {
        if (millis() - s_bgStartedAt > kBgConnectTimeoutMs) {
            s_bgState = BgState::Idle;
            BookOrbit.setForegroundConnectHook(nullptr);
            BookOrbit.disconnectWiFi();
            Serial.println("[Sync] Background upload: no Wi-Fi; progress stays queued (dirty).");
        }
        return;
    }
    // Connected: one short exchange (a request or two), then the radio goes off.
    KeepToken keepToken(ctx);
    s_bgState = BgState::Idle;
    BookOrbit.setForegroundConnectHook(nullptr);
    const uint32_t t0 = millis();
    BookProgressRecord* rec = ProgressStore::instance().find(s_bgFileId, s_bgPath);
    if (rec && (rec->state.localDirty || !s_bgOnlyIfDirty)) {
        const int pageBefore = Reader.isOpened() ? Reader.getCurrentPage() : 0;
        ensureToken(ctx);
        const bool ok = syncBookProgress(ctx, *rec);
        Serial.printf("[Sync] Background sync %s in %lu ms (Wi-Fi took %lu ms)\n", ok ? "done" : "failed",
                      (unsigned long)(millis() - t0), (unsigned long)(t0 - s_bgStartedAt));
        // The server had a newer position for the open book: show that page.
        if (ctx.currentState == STATE_READING && Reader.isOpened() && Reader.getCurrentPage() != pageBefore) {
            Reader.renderCurrentPage();
        }
    }
    BookOrbit.disconnectWiFi();
}
