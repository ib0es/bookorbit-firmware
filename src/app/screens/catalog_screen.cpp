#include "screens.h"
#include "../../hal/storage.h"
#include "../../services/library_manager.h"
#include "../../ui/views/catalog_view.h"
#include "../../ui/views/library_view.h"
#include "../../ui/views/home_view.h"
#include "../../ui/keyboard_ui.h"
#include "../../ui/i18n.h"

namespace CatalogScreen {

void handleEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;

    switch (event.id) {
        case BTN_DOWN:
            ctx.selectedCatalogIndex = (ctx.selectedCatalogIndex + 1) % NUM_CATALOG_ITEMS;
            drawCatalogScreen(ctx, false);
            break;

        case BTN_UP:
            ctx.selectedCatalogIndex = (ctx.selectedCatalogIndex - 1 + NUM_CATALOG_ITEMS) % NUM_CATALOG_ITEMS;
            drawCatalogScreen(ctx, false);
            break;

        case BTN_BACK:
        case BTN_LEFT:   // the footer hint offers BACK/LEFT
            ctx.currentState = STATE_HOME;
            ctx.selectedHomeIndex = 1; // Return to Catalog option on Home
            drawHomeScreen(ctx, false);
            break;

        case BTN_CONFIRM:
        case BTN_RIGHT: {
            auto& lib = LibraryManager::getInstance();
            switch (ctx.selectedCatalogIndex) {
                case 0: { // Search
                    String query = lib.searchQuery;
                    if (KeyboardUI::show(tr(MsgId::CATALOG_SEARCH_TITLE), tr(MsgId::CATALOG_SEARCH_HINT), query, KB_MODE_TEXT)) {
                        lib.searchQuery = query;
                        lib.currentFilter = FILTER_SEARCH;
                        lib.currentFolder = "@search";
                        lib.selectedBookIndex = 0;
                        lib.scanLocalBooks();
                        ctx.currentState = STATE_LIBRARY;
                        drawLibraryScreen(ctx);
                    } else {
                        drawCatalogScreen(ctx, false);
                    }
                    break;
                }
                case 1: // All books
                    lib.currentFilter = FILTER_ALL;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 2: // Libraries
                    lib.currentFilter = FILTER_LIBRARIES;
                    lib.currentFolder = "@libraries";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 3: // By Authors
                    lib.currentFilter = FILTER_AUTHORS;
                    lib.currentFolder = "@authors";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 4: // By Series
                    lib.currentFilter = FILTER_SERIES;
                    lib.currentFolder = "@series";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 5: // SD Folders
                    lib.currentFilter = FILTER_FOLDERS;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 6: // Downloaded
                    lib.currentFilter = FILTER_DOWNLOADED;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 7: // Completed
                    lib.currentFilter = FILTER_COMPLETED;
                    lib.currentFolder = "/books";
                    lib.selectedBookIndex = 0;
                    lib.scanLocalBooks();
                    ctx.currentState = STATE_LIBRARY;
                    drawLibraryScreen(ctx);
                    break;

                case 8: // Hide completed toggle
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
