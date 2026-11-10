// CFIs of FB2 books converted here <-> the web reader's (foliate-js). Run: test/host/run.sh
// The converter's maps against real foliate are checked by tools/fb2_cfi_check.py.
#include "reader/cfi_map.h"

#include <cstdio>
#include <cstring>
#include <string>

using EpubCfi::Location;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string pub(const CfiMap& m, const char* ours) {
    Location l;
    if (!EpubCfi::parse(ours, l) || !m.toPublic(l)) return "-";
    return EpubCfi::format(l);
}
static std::string loc(const CfiMap& m, const char* theirs) {
    Location l;
    if (!EpubCfi::parse(theirs, l) || !m.toLocal(l)) return "-";
    return EpubCfi::format(l);
}
#define ROUND(m, ours, theirs) do { CHECK(pub(m, ours) == theirs); CHECK(loc(m, theirs) == ours); \
    if (pub(m, ours) != theirs) printf("    pub %s -> %s (want %s)\n", ours, pub(m, ours).c_str(), theirs); \
    if (loc(m, theirs) != ours) printf("    loc %s -> %s (want %s)\n", theirs, loc(m, theirs).c_str(), ours); } while (0)

int main() {
    // Files: 1 = section 0 (the title); 2, 3, 4 = section 1, cut inside its child 1
    // (before that child's child 7), then before its child 900; 5 = section 2. A
    // long paragraph in file 4 at 4/2/6 (two pieces: the second after 1 child
    // element, 40 units into the text there).
    const char* text =
        "fb2cfi 1\n"
        "F 1 0\n"
        "F 2 1\n"
        "F 3 1 1 7\n"
        "F 4 1 900\n"
        "F 5 2\n"
        "L 4 4/2/6 0 0 1 40\n";
    CfiMap m;
    CHECK(m.parse(text, strlen(text)));
    CHECK(!m.empty());

    // Whole files: only the spine item changes.
    ROUND(m, "epubcfi(/6/2!/4/2/2/1:3)", "epubcfi(/6/2!/4/2/2/1:3)");
    ROUND(m, "epubcfi(/6/4!/4/2/4/10/1:5)", "epubcfi(/6/4!/4/2/4/10/1:5)");
    ROUND(m, "epubcfi(/6/10!/4/2/4/1:0)", "epubcfi(/6/6!/4/2/4/1:0)");
    // Cut inside child 1: in the chain's first element the next level shifts too, past it not.
    ROUND(m, "epubcfi(/6/6!/4/2/2/2/1:1)", "epubcfi(/6/4!/4/2/4/16/1:1)");
    ROUND(m, "epubcfi(/6/6!/4/2/2/6/3:2)", "epubcfi(/6/4!/4/2/4/20/3:2)");
    ROUND(m, "epubcfi(/6/6!/4/2/4/6/1:2)", "epubcfi(/6/4!/4/2/6/6/1:2)");
    // Cut before child 900.
    ROUND(m, "epubcfi(/6/8!/4/2/2/1:7)", "epubcfi(/6/4!/4/2/1802/1:7)");
    ROUND(m, "epubcfi(/6/8!/4/2/8/1:7)", "epubcfi(/6/4!/4/2/1808/1:7)");
    // A place before a file's start belongs to the file before.
    ROUND(m, "epubcfi(/6/6!/4/2/1798/1:0)", "epubcfi(/6/4!/4/2/1800/1:0)");
    ROUND(m, "epubcfi(/6/4!/4/2/4/14/1:0)", "epubcfi(/6/4!/4/2/4/14/1:0)");
    // The long paragraph (foliate's p 4/2/1806): piece 1 is our 4/2/6/2, piece 2 4/2/6/4.
    ROUND(m, "epubcfi(/6/8!/4/2/6/2/1:10)", "epubcfi(/6/4!/4/2/1806/1:10)");
    ROUND(m, "epubcfi(/6/8!/4/2/6/2/2/1:1)", "epubcfi(/6/4!/4/2/1806/2/1:1)");
    ROUND(m, "epubcfi(/6/8!/4/2/6/2/3:30)", "epubcfi(/6/4!/4/2/1806/3:30)");
    ROUND(m, "epubcfi(/6/8!/4/2/6/4/1:5)", "epubcfi(/6/4!/4/2/1806/3:45)");
    ROUND(m, "epubcfi(/6/8!/4/2/6/4/3:5)", "epubcfi(/6/4!/4/2/1806/5:5)");
    // The paragraph itself (the div) and unknown places.
    CHECK(pub(m, "epubcfi(/6/8!/4/2/6)") == "epubcfi(/6/4!/4/2/1806)");
    CHECK(loc(m, "epubcfi(/6/4!/4/2/1806)") == "epubcfi(/6/8!/4/2/6/2)");
    CHECK(pub(m, "epubcfi(/6/14!/4/2/2/1:0)") == "-");
    CHECK(loc(m, "epubcfi(/6/14!/4/2/2/1:0)") == "-");
    // The <body> itself: the section's start.
    CHECK(pub(m, "epubcfi(/6/4!/4)") == "epubcfi(/6/4!/4)");

    // cfimap 2 (KF8): offsets from <body>'s children. File 2 starts inside
    // body's child 3, before its child 5; file 3 before body's child 40.
    const char* v2 = "cfimap 2\nF 1 0\nF 2 0 3 5\nF 3 0 40\nL 3 4/4 0 0 2 7\n";
    CfiMap k;
    CHECK(k.parse(v2, strlen(v2)));
    ROUND(k, "epubcfi(/6/2!/4/6/1:2)", "epubcfi(/6/2!/4/6/1:2)");
    ROUND(k, "epubcfi(/6/4!/4/2/2/1:2)", "epubcfi(/6/2!/4/8/12/1:2)");
    ROUND(k, "epubcfi(/6/4!/4/4/2/1:2)", "epubcfi(/6/2!/4/10/2/1:2)");
    ROUND(k, "epubcfi(/6/6!/4/2/1:0)", "epubcfi(/6/2!/4/82/1:0)");
    ROUND(k, "epubcfi(/6/6!/4/4/4/1:3)", "epubcfi(/6/2!/4/84/5:10)");
    ROUND(k, "epubcfi(/6/2!/4/8/10/1:0)", "epubcfi(/6/2!/4/8/10/1:0)");

    CfiMap bad;
    CHECK(!bad.parse("hello\n", 6) && bad.empty());
    CHECK(!bad.parse("fb2cfi 1\nF 2 0\n", 15) && bad.empty());   // file 1 missing

    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
