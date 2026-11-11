#pragma once

// Getting the device ready for the radio. On the small memory tier (C3, no
// PSRAM) Wi-Fi needs the memory a background layout holds and the font caches
// (~25 KB, filled again as pages are drawn), so both are given up first; on the
// large tier the reader keeps them. Every path that brings Wi-Fi up starts here.
namespace WifiSession {
void prepare();
}
