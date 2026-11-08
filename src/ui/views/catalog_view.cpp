#include "catalog_view.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../services/library_manager.h"

using namespace uikit;

namespace {

struct CatalogItemDef {
    const char* title;
    String value;
    const char* tag;
};

// Counts come from LibraryManager::counts (kept current by the library), so a
// redraw (every UP/DOWN) does not walk the book list or the SD card.
CatalogItemDef getCatalogItemDef(const AppContext& ctx, int index) {
    auto& lib = LibraryManager::getInstance();
    const LibraryManager::Counts& c = lib.counts;
    switch (index) {
        case 0: {
            String sub = lib.searchQuery.length() > 0 ? ("\"" + lib.searchQuery + "\"") : String(tr(MsgId::CATALOG_SEARCH_HINT));
            return { tr(MsgId::CATALOG_SEARCH), sub, tr(MsgId::CATALOG_TAG_SEARCH) };
        }
        case 1: return { tr(MsgId::CATALOG_ALL_BOOKS), String(c.total), tr(MsgId::CATALOG_TAG_ALL) };
        case 2: return { tr(MsgId::CATALOG_BY_LIBRARIES), String(c.libraries), tr(MsgId::CATALOG_TAG_LIBRARIES) };
        case 3: return { tr(MsgId::CATALOG_BY_AUTHORS), String(c.authors), tr(MsgId::CATALOG_TAG_AUTHORS) };
        case 4: return { tr(MsgId::CATALOG_BY_SERIES), String(c.series), tr(MsgId::CATALOG_TAG_SERIES) };
        case 5: return { tr(MsgId::CATALOG_BY_FOLDERS), String(c.folders), tr(MsgId::CATALOG_TAG_FOLDERS) };
        case 6: return { tr(MsgId::CATALOG_DOWNLOADED), String(c.downloaded), tr(MsgId::CATALOG_TAG_DOWNLOADED) };
        case 7: return { tr(MsgId::CATALOG_COMPLETED), String(c.completed), tr(MsgId::CATALOG_TAG_COMPLETED) };
        case 8:
            return { tr(MsgId::CATALOG_HIDE_COMPLETED),
                     ctx.config.hideCompleted ? tr(MsgId::CATALOG_HIDE_ON) : tr(MsgId::CATALOG_HIDE_OFF),
                     tr(MsgId::CATALOG_TAG_OPTION) };
        default:
            return { "", "", "" };
    }
}

} // namespace

void drawCatalogScreen(AppContext& ctx, bool fullRefresh) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();

    Rect area = topBar(f, tr(MsgId::CATALOG_TITLE));
    area = footerHints(f, area, tr(MsgId::SETTINGS_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK));
    area = insetXY(area, kMargin, 8);

    const int count = NUM_CATALOG_ITEMS;
    int16_t cardH = (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 16);
    int16_t gap = 4;
    const int16_t minCardH = (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 4);
    if (count > 0 && count * (cardH + gap) > area.height) {
        gap = 3;
        cardH = (int16_t)(area.height / count - gap);
        if (cardH < minCardH) cardH = minCardH;
    }

    for (int i = 0; i < count; i++) {
        CatalogItemDef item = getCatalogItemDef(ctx, i);
        card(f, takeTop(area, cardH), item.title, item.value.c_str(), item.tag,
             i == ctx.selectedCatalogIndex, 1, (int16_t)i);
        takeTop(area, gap);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawCatalogScreen finished in %lu ms.\n", millis() - t0);
}
