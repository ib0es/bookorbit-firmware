#pragma once
// Test stub (test/sim): a random number that is always 7, so the portal's PIN (a home
// Wi-Fi session) is 777777 and its access point password 77777777: the tests know them.
#include <cstdint>

inline uint32_t esp_random() { return 7; }
