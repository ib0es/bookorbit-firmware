// Host unit tests for utf8Prefix (src/ui/text_utils.h).  Run: test/host/run.sh
#include "ui/text_utils.h"

#include <cstdio>
#include <cstring>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Every byte of `s` that starts a character is followed by its whole sequence.
static bool wholeCharacters(const String& s) {
    for (size_t i = 0; i < s.length();) {
        const uint8_t c = (uint8_t)s[i];
        const size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
        if (n == 0 || i + n > s.length()) return false;
        for (size_t k = 1; k < n; k++) if (((uint8_t)s[i + k] & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

int main() {
    // Short enough: unchanged.
    CHECK(utf8Prefix("Alice", 32) == "Alice");
    CHECK(utf8Prefix("", 4) == "");
    // ASCII: exactly the bytes asked for.
    CHECK(utf8Prefix("abcdefgh", 3) == "abc");
    // "Дракон Возрожденный" is 37 bytes: 32 used to cut the letter "е" in two.
    const String title = "Дракон Возрожденный";
    CHECK(title.length() == 37);
    for (size_t n = 0; n <= 40; n++) {
        const String p = utf8Prefix(title, n);
        CHECK(p.length() <= n);
        CHECK(p.length() + 1 >= (n < title.length() ? n : title.length()));   // loses at most one byte of a letter
        CHECK(wholeCharacters(p));
        CHECK(strncmp(p.c_str(), title.c_str(), p.length()) == 0);
    }
    CHECK(utf8Prefix(title, 32) == "Дракон Возрожден");
    // Three- and four-byte characters (€, an emoji) are kept whole too.
    const String mixed = "a€b😀c";
    for (size_t n = 0; n <= mixed.length(); n++) CHECK(wholeCharacters(utf8Prefix(mixed, n)));
    CHECK(utf8Prefix(mixed, 3) == "a");
    CHECK(utf8Prefix(mixed, 4) == "a€");
    CHECK(utf8Prefix(mixed, 8) == "a€b");
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
