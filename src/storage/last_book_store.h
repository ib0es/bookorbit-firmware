#pragma once

#include "book_records.h"

// The book being read, /books/last_book.json.
namespace LastBookStore {
bool save(const LastBookInfo& info);
// false when there is none (or it names no file).
bool load(LastBookInfo& outInfo);
}
