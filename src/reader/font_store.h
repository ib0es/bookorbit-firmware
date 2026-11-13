#pragma once

// Multiple memory-mapped TrueType families in the dedicated Flash partition.
// Two catalog sectors provide atomic updates; legacy single-family data is adopted.

#include <Arduino.h>
#include <stdint.h>

#include <vector>

namespace FontStore {

struct Family {
    String name;
    String files[4];       // by style: 0 regular, 1 bold, 2 italic, 3 bold italic ("" = none)
    uint32_t bytes = 0;    // all its files
};

constexpr const char* kDir = "/fonts";

// The families in /fonts, sorted by name.
std::vector<Family> scanCard();

// Partition capacity for a family's files (0 if there is no partition).
uint32_t capacity();

// The family installed in the partition ("" if none or broken).
String installedName(); // compatibility: first installed family
std::vector<Family> installedFamilies();
bool contains(const String& name);
uint32_t freeBytes();
uint32_t largestFreeBytes();
bool canInstall(const Family& family);

// Exact match of the family faces against the installed Flash data.
bool matchesInstalled(const Family& family);
String familyNameForFile(const String& file);
bool sameFamilyName(const String& left, const String& right);
bool removeInstalled(const String& name);
// Remove all matching face files on the card; does not touch Flash.
bool removeCard(const String& name);

// Adds/replaces one family, retaining every other installed family. `progress`
// gets 0..1000. Whatever reads the installed faces must have let go of them
// (unmap() is called first).
using ProgressFn = void (*)(void* user, int permille);
bool install(const Family& family, ProgressFn progress, void* user);

// The installed family's face files, read in place; null when absent. map()
// once, then face() as often as needed; unmap() before an install.
bool map(const String& name);
void unmap();
const uint8_t* face(int style, uint32_t* len);

}  // namespace FontStore
