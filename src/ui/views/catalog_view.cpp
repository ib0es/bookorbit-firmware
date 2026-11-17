#include "catalog_view.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../services/library_manager.h"

using namespace uikit;

namespace {

struct CatalogItemDef {
    const char* title = nullptr;
    String value;
    const char* tag = nullptr;
};

// Counts come from LibraryManager::counts (kept current by the library), so a
// redraw (every UP/DOWN) does not walk the book list or the SD card.
CatalogItemDef getCatalogItemDef(const AppContext& ctx, int index) {
    auto& lib = LibraryManager::getInstance();
    const LibraryManager::Counts& c = lib.getCounts();
    switch (index) {
        case 0:
            return {tr(MsgId::CATALOG_ALL_BOOKS), String(c.total), tr(MsgId::CATALOG_TAG_ALL)};
        case 1:
            return {tr(MsgId::CATALOG_BY_LIBRARIES), String(c.libraries), tr(MsgId::CATALOG_TAG_LIBRARIES)};
        case 2:
            return {tr(MsgId::CATALOG_BY_AUTHORS), String(c.authors), tr(MsgId::CATALOG_TAG_AUTHORS)};
        case 3:
            return {tr(MsgId::CATALOG_BY_SERIES), String(c.series), tr(MsgId::CATALOG_TAG_SERIES)};
        case 4:
            return {tr(MsgId::CATALOG_BY_FOLDERS), String(c.folders), tr(MsgId::CATALOG_TAG_FOLDERS)};
        case 5:
            return {tr(MsgId::CATALOG_DOWNLOADED), String(c.downloaded), tr(MsgId::CATALOG_TAG_DOWNLOADED)};
        case 6:
            return {tr(MsgId::CATALOG_COMPLETED), String(c.completed), tr(MsgId::CATALOG_TAG_COMPLETED)};
        case 7:
            return {tr(MsgId::CATALOG_HIDE_COMPLETED),
                    ctx.config.hideCompleted ? tr(MsgId::CATALOG_HIDE_ON) : tr(MsgId::CATALOG_HIDE_OFF),
                    tr(MsgId::CATALOG_TAG_OPTION)};
        default:
            return {"", "", ""};
    }
}

} // namespace

void drawCatalogSourcesScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::CATALOG_TITLE));
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_OPEN), tr(MsgId::KEY_BACK), tr(MsgId::KEY_OPEN)});
    area = insetXY(area, kMargin, 2);
    auto& lib = LibraryManager::getInstance();
    for (int i = 0; i < NUM_CATALOG_SOURCES; ++i) {
        const bool online = i == CATALOG_ONLINE;
        const bool search = i == CATALOG_SEARCH;
        const String value = search ? lib.searchQuery : String(online ? lib.getCounts().total : lib.offlineCount());
        RowOpts opts;
        opts.chevron = true;
        opts.value = value.c_str();
        opts.leadingIcon = search ? icon::search : online ? icon::cloudOutline : icon::cloudOff;
        const MsgId label = search ? MsgId::CATALOG_SEARCH : online ? MsgId::CATALOG_ONLINE : MsgId::HOME_TILE_OFFLINE;
        row(f, takeTop(area, 60), tr(label), nullptr, opts, i == ctx.selectedCatalogSource, 1, i);
        takeTop(area, 4);
    }
    present(fullRefresh);
    Serial.println("[UI] drawCatalogSourcesScreen");
}

void drawCatalogScreen(AppContext& ctx, bool fullRefresh) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();

    Rect area = topBar(f, tr(MsgId::CATALOG_ONLINE));
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_OPEN), tr(MsgId::KEY_BACK), tr(MsgId::KEY_OPEN)});
    area = insetXY(area, kMargin, 2);
    const int count = NUM_CATALOG_ITEMS;
    // Lists with their counts and the "hide completed" switch.
    const int16_t rowH = rowHeight(false);
    const CardList l = cardList(area, count, ctx.selectedCatalogIndex, rowH, 4);
    for (int i = l.first; i < count && i < l.first + l.perPage; i++) {
        CatalogItemDef item = getCatalogItemDef(ctx, i);
        RowOpts o;
        const char* sub = nullptr;
        if (i == NUM_CATALOG_ITEMS - 1) {
            o.toggle = ctx.config.hideCompleted ? 1 : 0;
        } else {
            o.value = item.value.c_str();
            o.chevron = true;
        }
        row(f, takeTop(area, l.cardH), item.title, sub, o, i == ctx.selectedCatalogIndex, 1, (int16_t)i);
        takeTop(area, l.gap);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawCatalogScreen finished in %lu ms.\n", millis() - t0);
}
