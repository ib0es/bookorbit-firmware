#include "screens.h"
#include "../../platform/hal/storage.h"
#include "../../services/library_manager.h"
#include "../../ui/views/catalog_view.h"
#include "../../ui/views/library_view.h"
#include "../../ui/views/home_view.h"
#include "../../ui/keyboard_ui.h"
#include "../../ui/i18n.h"
#include "../../ui/actions.h"

namespace CatalogScreen {
namespace {
void handleSources(AppContext& ctx, Action action) {
    switch (action) {
        case Action::Up:
        case Action::Down:
            ctx.selectedCatalogSource =
                    (ctx.selectedCatalogSource + (action == Action::Up ? -1 : 1) + NUM_CATALOG_SOURCES) %
                    NUM_CATALOG_SOURCES;
            drawCatalogSourcesScreen(ctx);
            break;
        case Action::Back:
        case Action::Prev:
            ctx.currentState = STATE_HOME;
            ctx.selectedHomeIndex = HOME_CATALOG;
            drawHomeScreen(ctx, false);
            break;
        case Action::Select:
        case Action::Next: {
            auto& lib = LibraryManager::getInstance();
            if (ctx.selectedCatalogSource == CATALOG_OFFLINE) {
                lib.currentFilter = FILTER_OFFLINE;
                lib.currentFolder = LibraryManager::kOfflineDir;
                lib.selectedBookIndex = 0;
                lib.scanOfflineBooks();
                lib.rebuildFilteredIndices();
                ctx.currentState = STATE_LIBRARY;
                drawLibraryScreen(ctx);
            } else if (ctx.selectedCatalogSource == CATALOG_SEARCH) {
                String query = lib.searchQuery;
                if (KeyboardUI::show(tr(MsgId::CATALOG_SEARCH_TITLE), tr(MsgId::CATALOG_SEARCH_HINT), query,
                                     KB_MODE_TEXT)) {
                    lib.searchQuery = query;
                    Serial.printf("[Library] Search '%s'\n", query.c_str());
                    lib.currentFilter = FILTER_SEARCH;
                    lib.currentFolder = "@search";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                } else {
                    drawCatalogSourcesScreen(ctx);
                }
            } else {
                lib.refreshLocalFilesIfChanged();
                ctx.currentState = STATE_CATALOG;
                ctx.selectedCatalogIndex = 0;
                drawCatalogScreen(ctx, false);
            }
            break;
        }
        default:
            break;
    }
}
}  // namespace

void handleEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;

    const Action action = Actions::of(event);
    if (ctx.currentState == STATE_CATALOG_SOURCES) return handleSources(ctx, action);
    switch (action) {
        case Action::Down:
            ctx.selectedCatalogIndex = (ctx.selectedCatalogIndex + 1) % NUM_CATALOG_ITEMS;
            drawCatalogScreen(ctx, false);
            break;

        case Action::Up:
            ctx.selectedCatalogIndex = (ctx.selectedCatalogIndex - 1 + NUM_CATALOG_ITEMS) % NUM_CATALOG_ITEMS;
            drawCatalogScreen(ctx, false);
            break;

        case Action::Back:
        case Action::Prev:   // the footer hint offers BACK/LEFT
            ctx.currentState = STATE_CATALOG_SOURCES;
            ctx.selectedCatalogSource = CATALOG_ONLINE;
            drawCatalogSourcesScreen(ctx);
            break;

        case Action::Select:
        case Action::Next: {
            auto& lib = LibraryManager::getInstance();
            switch (ctx.selectedCatalogIndex) {
                case 0: // All books
                    lib.currentFilter = FILTER_ALL;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 1: // Libraries
                    lib.currentFilter = FILTER_LIBRARIES;
                    lib.currentFolder = "@libraries";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 2: // By Authors
                    lib.currentFilter = FILTER_AUTHORS;
                    lib.currentFolder = "@authors";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 3: // By Series
                    lib.currentFilter = FILTER_SERIES;
                    lib.currentFolder = "@series";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 4: // SD Folders
                    lib.currentFilter = FILTER_FOLDERS;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 5: // Downloaded
                    lib.currentFilter = FILTER_DOWNLOADED;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 6: // Completed
                    lib.currentFilter = FILTER_COMPLETED;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 7: // Hide completed toggle
                    ctx.config.hideCompleted = !ctx.config.hideCompleted;
                    lib.setHideCompleted(ctx.config.hideCompleted);
                    ctx.config.save();
                    drawCatalogScreen(ctx, false);
                    break;
            }
            break;
        }

        default:
            break;
    }
}

} // namespace CatalogScreen
