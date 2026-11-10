// Host unit tests for BookFormats.  Run: test/host/run.sh
#include "net/book_formats.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

using namespace BookFormats;

static void testMime() {
    // MIME types Book Orbit sends (opds_alice.xml), with parameters and in any case.
    CHECK(fromMime("application/epub+zip") == Epub);
    CHECK(fromMime("Application/EPUB+zip; charset=binary ") == Epub);
    CHECK(fromMime("application/x-fictionbook+xml") == Fb2);
    CHECK(fromMime("application/x-fictionbook") == Fb2);
    CHECK(fromMime("text/fb2+xml") == Fb2);
    CHECK(fromMime("application/x-mobipocket-ebook") == Mobi);
    CHECK(fromMime("application/x-mobi8-ebook") == Mobi);
    CHECK(fromMime("application/vnd.amazon.ebook", "AZW3") == Azw3);
    CHECK(fromMime("application/vnd.amazon.ebook", "AZW") == Azw);
    CHECK(fromMime("application/vnd.amazon.ebook") == Azw3);
    CHECK(fromMime("application/x-mobi8") == Azw3);
    CHECK(fromMime("text/plain") == Txt);
    // No usable type: the title, then the address.
    CHECK(fromMime("application/octet-stream", "fb2") == Fb2);
    CHECK(fromMime("", "", "/dl/99.epub") == Epub);
    CHECK(fromMime("", "", "/dl/book.MOBI?x=1") == Mobi);
    CHECK(fromMime("", "", "/download?fileId=1") == Unknown);
    CHECK(fromMime("") == Unknown);
}

static void testPaths() {
    CHECK(fromPath("/books/A.epub") == Epub);
    CHECK(fromPath("/books/A.FB2") == Fb2);
    CHECK(fromPath("a.mobi") == Mobi);
    CHECK(fromPath("a.azw3") == Azw3);
    CHECK(fromPath("a.azw") == Azw);
    CHECK(fromPath("a.txt") == Txt);
    CHECK(fromPath("a.fb2.zip") == Unknown);
    CHECK(fromPath("epub") == Unknown);
    CHECK(fromPath("") == Unknown);
    CHECK(std::string(extension(Fb2)) == ".fb2" && std::string(extension(Unknown)).empty());
    CHECK(std::string(label(Azw3)) == "AZW3" && std::string(label((Kind)200)) == "?");
    CHECK(readable(Epub) && readable(Txt) && readable(Fb2) && readable(Mobi) && readable(Azw) && readable(Azw3) &&
          !readable(Unknown));
    CHECK(rank(Epub) < rank(Azw3) && rank(Azw3) < rank(Fb2) && rank(Fb2) < rank(Mobi) && rank(Mobi) < rank(Azw));
}

static void testUrls() {
    CHECK(downloadPath("4", 7) == "/api/v1/opds/4/download?fileId=7");
    CHECK(compactUrl("/api/v1/opds/4/download?fileId=7", "4", 7).empty());
    CHECK(compactUrl("http://nas:6060/api/v1/opds/4/download?fileId=7", "4", 7).empty());
    CHECK(compactUrl("http://nas:6060/x/api/v1/opds/4/download?fileId=7", "4", 7) ==
          "http://nas:6060/x/api/v1/opds/4/download?fileId=7");   // another path on the host
    CHECK(compactUrl("/api/v1/opds/4/download?fileId=7&selection=x", "4", 7) ==
          "/api/v1/opds/4/download?fileId=7&selection=x");
    CHECK(compactUrl("/api/v1/opds/4/download?fileId=7", "4", 8) == "/api/v1/opds/4/download?fileId=7");
    CHECK(compactUrl("/api/v1/opds/ab/download?fileId=7", "ab", 7) == "/api/v1/opds/ab/download?fileId=7");   // not numeric
    CHECK(compactUrl("/x", "", 7) == "/x");
    Ref r; r.fileId = 5;
    CHECK(urlFor(r, "4") == "/api/v1/opds/4/download?fileId=5");
    CHECK(urlFor(r, "").empty());
    r.url = "/other";
    CHECK(urlFor(r, "4") == "/other");
    Ref none;
    CHECK(urlFor(none, "4").empty());
}

static void testPreferred() {
    std::vector<Ref> refs;
    CHECK(preferred(refs) == -1);
    Ref fb2; fb2.kind = Fb2;
    Ref mobi; mobi.kind = Mobi;
    Ref epub; epub.kind = Epub;
    Ref txt; txt.kind = Txt;
    Ref azw3; azw3.kind = Azw3;
    Ref unknown; unknown.kind = Unknown;
    refs = {unknown, unknown};
    CHECK(preferred(refs) == 0);          // nothing readable: the first
    refs = {unknown, txt, azw3};
    CHECK(preferred(refs) == 2);          // readable beats unknown; AZW3 ranks before TXT
    refs = {txt, fb2};
    CHECK(preferred(refs) == 1);
    refs = {txt, fb2, epub, mobi};
    CHECK(preferred(refs) == 2);
    refs = {epub, txt};
    CHECK(preferred(refs) == 0);
}

static void testLabels() {
    CHECK(labels({}) == "");
    CHECK(labels({Fb2}) == "FB2");
    CHECK(labels({Mobi, Fb2, Epub, Azw3, Fb2}) == "EPUB \xC2\xB7 AZW3 \xC2\xB7 FB2 \xC2\xB7 MOBI");
}

int main() {
    testMime();
    testPaths();
    testUrls();
    testPreferred();
    testLabels();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
