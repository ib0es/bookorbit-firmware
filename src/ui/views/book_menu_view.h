#pragma once
#include <Arduino.h>
#include "../../app/app_state.h"
#include "../../services/library_manager.h"

// Actions of the per-book menu (library: LEFT key, or a long press on a book).
enum BookMenuAction { BOOK_MENU_READ, BOOK_MENU_DOWNLOAD, BOOK_MENU_SYNC, BOOK_MENU_DELETE, BOOK_MENU_BACK };

// The menu rows for `book`, in display order. Returns their count (<= 5).
int bookMenuItems(const BookItem& book, BookMenuAction out[5]);

// The book the menu is about: the library's current selection (nullptr if none).
BookItem* bookMenuBook();

void drawBookMenuScreen(AppContext& ctx, bool fullRefresh = false);
