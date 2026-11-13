// Hyphenation patterns that ship with the firmware.  Run: test/host/run.sh
// Expected splits come from a plain Liang implementation over the TeX source
// patterns (hyph-ru.pat.txt, hyph-en-us), with the engine's minimums: two
// letters before the first break, three after the last.
#include "reader/engine/hyph_ru.h"

#include <text/Hyphenator.h>
#include <text/hyph_en_us.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace freeink::book;

static int g_failed = 0, g_checks = 0;

static std::string split(const Hyphenator& h, const char* word) {
    uint8_t pos[32];
    const uint8_t n = h.breakPositions(word, (uint32_t)strlen(word), pos, sizeof(pos));
    std::string out;
    size_t prev = 0;
    for (uint8_t i = 0; i < n; i++) {
        out.append(word + prev, pos[i] - prev);
        out += '-';
        prev = pos[i];
    }
    return out + (word + prev);
}

static void expect(const Hyphenator& h, const char* word, const char* want) {
    g_checks++;
    const std::string got = split(h, word);
    if (got != want) {
        g_failed++;
        printf("  FAIL %s: got %s, want %s\n", word, got.c_str(), want);
    }
}

int main() {
    Hyphenator ru, en;
    g_checks += 2;
    if (!ru.init(k_hyph_ru, k_hyph_ru_size)) {
        g_failed++;
        printf("  FAIL ru patterns rejected\n");
    }
    if (!en.init(k_hyph_en_us, k_hyph_en_us_size)) {
        g_failed++;
        printf("  FAIL en patterns rejected\n");
    }

    expect(ru, "программирование", "про-грам-ми-ро-ва-ние");
    expect(ru, "путешествия", "пу-те-ше-ствия");
    expect(ru, "перелистывать", "пе-ре-ли-сты-вать");
    expect(ru, "преступление", "пре-ступ-ле-ние");
    expect(ru, "наказание", "на-ка-за-ние");
    expect(ru, "симулятора", "си-му-ля-тора");
    // Capitals fold to the lower-case patterns; offsets stay on the original bytes.
    expect(ru, "Электронная", "Элек-трон-ная");
    expect(ru, "Достоевский", "До-сто-ев-ский");
    expect(ru, "Здравствуйте", "Здрав-ствуйте");
    // Too short to split (2 + 3 letters at least), ё included.
    expect(ru, "книга", "книга");
    expect(ru, "ёлка", "ёлка");
    // Each language leaves the other script alone.
    expect(en, "путешествия", "путешествия");
    expect(ru, "hyphenation", "hyphenation");
    expect(en, "hyphenation", "hy-phen-ation");

    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
