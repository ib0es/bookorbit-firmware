#pragma once

// Book fonts beyond the built-in family: TrueType families in /fonts on the
// card, one of them installed at a time into the flash partition the firmware
// does not otherwise use (the "spiffs" data partition, ~3.4 MB), where it is
// read in place (memory-mapped) like the built-in one: a family's files are
// far too big for the heap.
//
// A family is the files of /fonts whose names share a stem ("Literata" for
// Literata-Regular.ttf, Literata-Italic.ttf...); each file's style (bold,
// italic) comes from its 'head' table, else from its name. Faces missing
// from a family are drawn from its regular one (bold by double strike).
//
// The partition holds a header (family name, faces' offsets and sizes)
// written last: an install cut short (power, card pulled) leaves no family.
// In the simulator the partition is a file on the card, read into memory.

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
String installedName();

// Copies a family into the partition (erasing the one there). `progress`
// gets 0..1000. Whatever reads the installed faces must have let go of them
// (unmap() is called first).
using ProgressFn = void (*)(void* user, int permille);
bool install(const Family& family, ProgressFn progress, void* user);

// The installed family's face files, read in place; null when absent. map()
// once, then face() as often as needed; unmap() before an install.
bool map();
void unmap();
const uint8_t* face(int style, uint32_t* len);

}  // namespace FontStore
