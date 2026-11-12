#pragma once

// What the parts of LibraryManager share (A8: library_manager.cpp was split
// into the list model, covers, card clean-up and file names, and the current
// book's progress). Internal to src/services/library_*.cpp.

#include <Arduino.h>
#include <string>

namespace LibraryInternal {
// library_cleanup.cpp
String stemOf(const String& path);
void removeIfExists(const String& path);
void removeCoverStem(const String& stem);
// library_manager.cpp (the list model)
String withoutExtension(const String& path);
std::string bookKey(const String& path);   // folded "<dir>/<stem>": formats of one book share it
bool isBookFile(const String& name);
// library_covers.cpp
String epubCoverPath(const String& filePath);
bool isEpubPath(const String& filePath);
}
