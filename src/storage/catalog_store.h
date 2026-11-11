#pragma once

#include <vector>
#include "book_records.h"

// The catalog cache /books/catalog.json: the books of the server, as the
// last sync saw them.
namespace CatalogStore {
bool save(const std::vector<BookMetadata>& catalog);
// false when there is no readable catalog (the next sync writes a new one).
bool load(std::vector<BookMetadata>& outCatalog);
}
