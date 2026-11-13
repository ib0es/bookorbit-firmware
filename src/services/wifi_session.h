#pragma once

// Getting the device ready for the radio. On the small memory tier (C3, no
// PSRAM) Wi-Fi needs the memory a background layout holds and the font caches
// (~25 KB, filled again as pages are drawn), so both are given up first; on the
// large tier the reader keeps them. Every path that brings Wi-Fi up starts here.
#include <cstdint>

namespace WifiSession {
void prepare();

// The other way round, before a book opens: on the small tier the radio and a
// background upload in progress would leave too little memory to build the
// book's index, so the upload is cancelled (its progress stays queued) and
// Wi-Fi goes off. On the large tier nothing changes (B9).
void giveMemoryToReader();

// Wi-Fi gives its memory back a moment after it is turned off: waits, at most
// `maxMs`, until the free heap stops growing (P2).
void waitForHeapToSettle(uint32_t maxMs = 500);
}  // namespace WifiSession
