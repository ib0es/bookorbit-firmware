#pragma once

// JSON files on the card (config, catalog, progress, last book, bookmarks).
//
// Reading parses straight from the file: no String copy and no size limit
// (SDCardManager::readFile stops at 50 KB).
//
// Writing serializes straight into "<path>.tmp" through a 512-byte buffer and
// swaps it in only when it is complete: the bytes written must be exactly
// measureJson() and the file on the card that long. A full String of a 60 KB
// catalog used to be built first, and a String that could not grow was cut
// silently, so a short file replaced a good one.
//
// A power cut between replaceFile's remove and rename leaves only a complete
// .tmp: read() takes it when the file itself is missing.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace JsonFile {

// EmptyInput when the file is missing.
DeserializationError read(const char* path, JsonDocument& doc);

// False (the file on the card untouched) if the document overflowed, the card
// refused a byte, or the swap failed.
bool write(const char* path, const JsonDocument& doc, bool pretty = false);

// Moves an unreadable file aside ("<path>.bad"), so that the next save does
// not write an empty list over data that may still be recovered by hand.
void keepUnreadable(const char* path);

}  // namespace JsonFile
