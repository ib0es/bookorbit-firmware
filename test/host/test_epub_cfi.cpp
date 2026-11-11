// Host unit tests for EPUB CFI parsing and the XHTML walker.  Run: test/host/run.sh
#include "core/epub_cfi.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace EpubCfi;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Minimal stand-in for the HTML stripper: emits text outside tags, a newline per
// block start tag, decodes &amp; and skips <head>. Enough to check the walker's
// bookkeeping; the real stripper is exercised by the simulator round trip.
class TextOnlySink : public TextSink {
public:
    std::string out;
    void feed(const char* d, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            const char c = d[i];
            if (inTag) {
                tag += c;
                if (tag.rfind("!--", 0) == 0) {   // comment: until "-->"
                    if (tag.size() >= 6 && tag.compare(tag.size() - 3, 3, "-->") == 0) inTag = false;
                    continue;
                }
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '"' || c == '\'') { quote = c; continue; }
                if (c == '>') {
                    inTag = false;
                    if (tag.rfind("head", 0) == 0) inHead = true;
                    if (tag.rfind("/head", 0) == 0) inHead = false;
                    if (tag.rfind("p", 0) == 0 || tag.rfind("div", 0) == 0) out += '\n';
                }
            } else if (c == '<') {
                inTag = true; tag.clear();
            } else if (!inHead) {
                if (ent.size() || c == '&') {
                    ent += c;
                    if (c == ';') { out += (ent == "&amp;") ? "&" : "?"; ent.clear(); }
                } else {
                    out += c;
                }
            }
        }
    }
    uint32_t emitted() const override { return (uint32_t)out.size(); }
private:
    bool inTag = false, inHead = false;
    char quote = 0;
    std::string tag, ent;
};

static const char* kDoc =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Око</title></head>\n"
    "<body class=\"z\">\n<span id=\"id12\"><div class=\"title1\">\n"
    "<p class=\"p\">Глава 10</p>\n<p class=\"p\"><em>Отъезд</em></p>\n"
    "</div><div class=\"image\"><img alt=\"a>b\" src=\"i.jpg\"/></div>"
    "<!-- note <p>x</p> --><p class=\"p1\">Единственный фонарь &amp; столб.</p><br/><p>Второй 😀 абзац.</p></span>\n"
    "</body></html>\n";

static uint32_t offsetOf(const std::string& cfi, bool* reached = nullptr, size_t chunk = 7) {
    Location loc;
    if (!parse(cfi, loc)) return 0xFFFFFFFF;
    TextOnlySink sink;
    Walker w = Walker::toOffset(sink, loc.steps, loc.offset);
    const std::string doc = kDoc;
    for (size_t i = 0; i < doc.size() && !w.done(); i += chunk) w.feed(doc.data() + i, std::min(chunk, doc.size() - i));
    w.finish();
    if (reached) *reached = w.reached();
    return w.textOffset();
}

static std::string textBefore(const std::string& needle) {
    TextOnlySink sink;
    sink.feed(kDoc, strlen(kDoc));
    return sink.out.substr(0, sink.out.find(needle));
}

static void testParseFormat() {
    printf("parse / format\n");
    Location l;
    CHECK(parse("epubcfi(/6/34!/4/2[id12]/2/2/1:0)", l));
    CHECK(l.ordinal == 17 && l.steps == std::vector<int>({4, 2, 2, 2, 1}) && l.offset == 0);
    CHECK(format(l) == "epubcfi(/6/34!/4/2/2/2/1:0)");
    CHECK(parse("epubcfi(/6/4[chap01ref]!/4[body01]/10[para05]/3:10[yyy;s=a])", l));
    CHECK(l.ordinal == 2 && l.steps == std::vector<int>({4, 10, 3}) && l.offset == 10);
    CHECK(parse("epubcfi(/6/14!/4/2,/1:0,/1:20)", l));               // range -> start
    CHECK(l.ordinal == 7 && l.steps == std::vector<int>({4, 2, 1}) && l.offset == 0);
    CHECK(parse("epubcfi(/6/8!/4/6)", l) && l.offset == 0 && format(l) == "epubcfi(/6/8!/4/6)");
    for (const char* bad : {"", "epubcfi()", "epubcfi(/6/34)", "epubcfi(/6/33!/4/1:0)", "epubcfi(/6!/4)",
                            "cfi(/6/2!/4)", "epubcfi(/6/2!/x)", "epubcfi(/6/2!/4[id)", "epubcfi(/6/2!/4/1:x)", "epubcfi(/6/2!)"}) {
        CHECK(!parse(bad, l));
    }
}

static void testToOffset() {
    printf("toOffset\n");
    bool reached = false;
    CHECK(offsetOf("epubcfi(/6/34!/4/2[id12]/2/2/1:0)", &reached) == textBefore("Глава").size() && reached);
    CHECK(offsetOf("epubcfi(/6/34!/4/2/2/2/1:6)") == textBefore("10").size());
    CHECK(offsetOf("epubcfi(/6/34!/4/2/2/4/2/1:3)") == textBefore("езд").size());   // inside <em>
    // After the image div (whose alt has '>') and a comment with tags in it.
    CHECK(offsetOf("epubcfi(/6/34!/4/2/6/1:13)") == textBefore("фонарь").size());
    CHECK(offsetOf("epubcfi(/6/34!/4/2/6/1:22)") == textBefore("столб").size());     // &amp; is one char
    // <br/> is element 8; the next paragraph is element 10. The emoji counts as 2 units.
    CHECK(offsetOf("epubcfi(/6/34!/4/2/10/1:10)") == textBefore("абзац").size());
    // Element target: right after its start tag.
    CHECK(offsetOf("epubcfi(/6/34!/4/2/6)") == textBefore("Единственный").size());
    // Missing nodes land on the next existing one; past the end -> end of text, not reached.
    // Offset past the end of a text node: the next node (the newline after </p>).
    CHECK(offsetOf("epubcfi(/6/34!/4/2/2/2/1:999)") == textBefore("Глава").size() + strlen("Глава 10"));
    // Text slot that does not exist (between <p class="p1"> and <br/> there is none): next node.
    CHECK(offsetOf("epubcfi(/6/34!/4/2/7:0)") == textBefore("Второй").size() - 1);
    CHECK(offsetOf("epubcfi(/6/34!/4/99/1:0)", &reached) == textBefore("@@none").size() && !reached);
    // Chunk size does not matter.
    for (size_t chunk : {1, 2, 3, 64, 4096})
        CHECK(offsetOf("epubcfi(/6/34!/4/2/6/1:22)", nullptr, chunk) == textBefore("столб").size());
}

static void testRoundTrip() {
    printf("toLocation -> toOffset round trip\n");
    TextOnlySink all;
    all.feed(kDoc, strlen(kDoc));
    int checked = 0, bad = 0;
    for (uint32_t target = 0; target <= all.out.size(); target++) {
        TextOnlySink sink;
        Walker w = Walker::toLocation(sink, target);
        w.feed(kDoc, strlen(kDoc));
        w.finish();
        if (!w.reached()) continue;   // nothing but whitespace after the target
        Location loc = w.location();
        loc.ordinal = 17;
        Location back;
        CHECK(parse(format(loc), back));
        TextOnlySink sink2;
        Walker w2 = Walker::toOffset(sink2, back.steps, back.offset);
        w2.feed(kDoc, strlen(kDoc));
        w2.finish();
        // Lands on the first non-space character at or after the target.
        uint32_t expect = target;
        while (expect < all.out.size() && (all.out[expect] == ' ' || all.out[expect] == '\n' ||
               ((unsigned char)all.out[expect] & 0xC0) == 0x80)) expect++;
        checked++;
        if (w2.textOffset() != expect) { bad++; if (bad < 5) printf("  target %u -> %s -> %u, want %u\n", target, format(loc).c_str(), w2.textOffset(), expect); }
    }
    printf("  %d positions\n", checked);
    CHECK(checked > 50);
    CHECK(bad == 0);
}

int main() {
    testParseFormat();
    testToOffset();
    testRoundTrip();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
