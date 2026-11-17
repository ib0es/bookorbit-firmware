#pragma once

#include <Arduino.h>
#include <BookStorage.h>

namespace OfflineMetadata {
struct Record {
    String title;
    String author;
    String coverPath;
    String series;
    double seriesIndex = 0; // 0 when absent; fractional positions are preserved.
};
using Cancelled = bool (*)();
// Bounded, metadata-only parsing; never converts a book or fetches a resource.
Record extract(freeink::book::BookSource& source, const String& path, Cancelled cancelled = nullptr);
String fallbackTitle(const String& path);
// Strict bounded decimal position, shared by native parsing and untrusted hints.
bool parseSeriesIndex(const String& text, double& value);
String cachePath(const String& path);
bool load(const String& path, Record& out);
// removed: the book leaves offlineBooks (deleted), else its file was replaced.
void invalidate(const String& path, bool removed = false);
// Validates untrusted portal hints and an optional baseline JPEG temporary file.
bool storeHints(const String& path, const String& title, const String& author, const String& coverTmp,
                const String& expectedSample = String(), const String& series = String(),
                const String& seriesIndex = String());
// Main-thread scheduler. The worker owns SD access until stop()/tick() joins it.
void reset();
void stop();
bool busy();
// startNext: false collects a finished book only; the next one starts from the idle path.
bool tick(bool startNext = true);
}  // namespace OfflineMetadata
