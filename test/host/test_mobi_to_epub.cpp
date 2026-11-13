// MOBI -> EPUB conversion. Run: test/host/run.sh
// A made-up Mobipocket book (loose HTML: unquoted attributes, unclosed tags,
// page breaks, filepos links, a contents page, recindex images, windows-1252)
// is converted and the EPUB opened and laid out by the engine; then every
// test/books/*.mobi there is (local books, not in the repository).
#include "convert/mobi_to_epub.h"
#include "convert_test_util.h"

#include <time.h>

// -------------------------------------------------------------- MOBI builder

static void put16(std::string& s, size_t at, uint16_t v) {
    s[at] = (char)(v >> 8);
    s[at + 1] = (char)v;
}
static void put32(std::string& s, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) s[at + i] = (char)(v >> (24 - 8 * i));
}
static std::string be32s(uint32_t v) {
    std::string s(4, '\0');
    put32(s, 0, v);
    return s;
}

// Text in uncompressed 4096-byte records, then the images.
static std::string buildMobi(const std::string& html, const std::vector<std::string>& images, uint32_t encoding,
                             uint16_t compression = 1, uint16_t encryption = 0, uint32_t version = 6) {
    std::vector<std::string> text;
    for (size_t at = 0; at < html.size(); at += 4096) text.push_back(html.substr(at, 4096));
    // EXTH: author, title, language, cover offset 0
    std::string exth;
    uint32_t count = 0;
    auto rec = [&](uint32_t type, const std::string& v) {
        exth += be32s(type) + be32s((uint32_t)v.size() + 8) + v;
        count++;
    };
    rec(100, "Test Author");
    rec(503, "Mobi Test");
    rec(524, "en");
    if (!images.empty()) rec(201, be32s(0));
    exth = "EXTH" + be32s((uint32_t)exth.size() + 12) + be32s(count) + exth;
    while (exth.size() % 4) exth += '\0';
    std::string r0(16 + 232, '\0');
    put16(r0, 0, compression);
    put32(r0, 4, (uint32_t)html.size());
    put16(r0, 8, (uint16_t)text.size());
    put16(r0, 10, 4096);
    put16(r0, 12, encryption);
    memcpy(&r0[16], "MOBI", 4);
    put32(r0, 20, 232);
    put32(r0, 24, 2);
    put32(r0, 28, encoding);
    put32(r0, 36, version);
    const uint32_t firstImage = (uint32_t)text.size() + 1;
    put32(r0, 0x6C, images.empty() ? 0xFFFFFFFF : firstImage);
    put32(r0, 0x80, 0x40);
    put16(r0, 0xF2, 0);
    r0 += exth;
    const std::string name = "Full Name";
    put32(r0, 0x54, (uint32_t)r0.size());
    put32(r0, 0x58, (uint32_t)name.size());
    r0 += name + std::string(4, '\0');

    std::vector<std::string> recs;
    recs.push_back(r0);
    for (auto& t : text) recs.push_back(t);
    for (auto& i : images) recs.push_back(i);
    recs.push_back(std::string("FLIS\0\0\0\x08", 8));   // a non-image record after the images
    std::string db(78, '\0');
    memcpy(&db[0], "mobi_test", 9);
    memcpy(&db[60], "BOOKMOBI", 8);
    put16(db, 76, (uint16_t)recs.size());
    uint32_t off = 78 + 8 * (uint32_t)recs.size() + 2;
    std::string list;
    for (size_t i = 0; i < recs.size(); i++) {
        list += be32s(off) + be32s((uint32_t)(i * 2));
        off += (uint32_t)recs[i].size();
    }
    db += list + std::string(2, '\0');
    for (auto& r : recs) db += r;
    return db;
}

// 1x1 PNG
static std::string png1() {
    static const unsigned char d[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
                                      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
                                      0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00, 0x00, 0x00,
                                      0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x60, 0x00, 0x00, 0x00,
                                      0x02, 0x00, 0x01, 0x48, 0xAF, 0xA4, 0x71, 0x00, 0x00, 0x00, 0x00, 0x49,
                                      0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    return std::string((const char*)d, sizeof(d));
}

static bool convertMobi(const std::string& mobi, std::string& epub, convert::ConvertStats& st,
                        convert::ConvertResult* result = nullptr) {
    MemIn in(mobi);
    MemOut out;
    MemTemp tmp;
    const convert::ConvertResult r = convert::mobiToEpub(in, out, tmp, nullptr, nullptr, &st);
    if (result) *result = r;
    epub = out.d;
    if (r != convert::ConvertResult::Ok) printf("  convert: %s %s\n", convert::resultName(r), st.error.c_str());
    return r == convert::ConvertResult::Ok;
}

static std::string fixtureHtml(uint32_t* chapterTwo, uint32_t* partB) {
    std::string h =
            "<html><head><title>x</title><style>p{a:b}</style><guide><reference type=\"toc\" title=\"Contents\" "
            "filepos=TOCPOS /></guide></head><body>";
    h += "<p height=\"1em\" width=\"0pt\" align=\"center\"><font size=\"7\"><b>The Title</b></font></p>";
    h += "<!-- a <comment> -->";
    h += "<p width=\"2em\">First <i>chapter</i> text &amp; more &nbsp;here, 5 & 6.<p>Unclosed before this one";
    h += "<p>Image: <img recindex=\"00001\" align=\"baseline\" width=\"10\" height=\"12\"></img></p>";
    h += "<p>Go to <a filepos=CH2POS>chapter two</a> or <a href=\"http://example.com/?a=1&b=2\">out</a>.</p>";
    h += "<blockquote>Quote<br>line two<br/>three</blockquote><center>centered</center>";
    h += "<table><tr><td colspan=2>cell<td>next<tr><td>row2</table>";
    h += "<mbp:pagebreak/>";
    const size_t ch2 = h.size();
    h += "<p><b>Chapter Two</b></p><p>Second chapter <font size=\"7\">big</font> text.</p><ul><li>one<li>two</ul>";
    h += "<p align=\"center\"><font size=\"6\">Head</font><br><font size=\"6\">line two</font> <font size=\"7\">bigger</font></p>";
    for (int i = 0; i < 1500; i++) h += "<p>Filler paragraph number " + std::to_string(i) + " for a long chapter.</p>";
    const size_t b = h.size();
    h += "<p>Part B marker</p><mbp:pagebreak/>";
    const size_t toc = h.size();
    h += "<p>Table of Contents</p><p><a filepos=CH1POS>Chapter One</a></p><p><a filepos=CH2POS>Chapter Two</a></p>"
         "<p><a filepos=PARTB>Part B</a></p><mbp:pagebreak/></body></html>";
    auto setNum = [&](const std::string& key, size_t v) {
        char n[16];
        snprintf(n, sizeof(n), "%010zu", v);
        for (size_t at; (at = h.find(key)) != std::string::npos;) h.replace(at, key.size(), n);
    };
    // Every placeholder is 10 characters long after replacement: offsets stay put
    // only if the placeholders are 10 too.
    setNum("TOCPOS", toc + 0);
    (void)toc;
    *chapterTwo = (uint32_t)ch2;
    *partB = (uint32_t)b;
    return h;
}

static void testFixture() {
    printf("fixture\n");
    uint32_t ch2 = 0, partB = 0;
    std::string html = fixtureHtml(&ch2, &partB);
    // Fix up the placeholders now that their lengths are known (to fixed points of the text).
    auto replaceAll = [&](const std::string& key, uint32_t v) {
        char n[16];
        snprintf(n, sizeof(n), "%010u", v);
        for (size_t at; (at = html.find(key)) != std::string::npos;) html.replace(at, key.size(), n);
    };
    // Offsets shift as placeholders are replaced; find the markers in the final text instead.
    replaceAll("CH1POS", 0);
    replaceAll("CH2POS", 0);
    replaceAll("PARTB", 0);
    ch2 = (uint32_t)html.find("<p><b>Chapter Two");
    partB = (uint32_t)html.find("<p>Part B marker");
    const uint32_t ch1 = (uint32_t)html.find("<p width=\"2em\">");
    const uint32_t toc = (uint32_t)html.find("<p>Table of Contents");
    auto setAt = [&](const char* after, uint32_t v, int nth) {
        size_t at = 0;
        for (int k = 0; k <= nth; k++) at = html.find(after, at) + strlen(after);
        char n[16];
        snprintf(n, sizeof(n), "%010u", v);
        html.replace(at, 10, n);
    };
    setAt("filepos=", toc, 0);                  // the guide
    setAt("filepos=", ch2, 1);                  // "chapter two" link
    setAt("filepos=", ch1, 2);                  // contents
    setAt("filepos=", ch2, 3);
    setAt("filepos=", partB, 4);
    std::string epub;
    convert::ConvertStats st;
    CHECK(convertMobi(buildMobi(html, {png1()}, 65001), epub, st));
    const Opened o = openEpub(epub);
    CHECK(o.ok);
    CHECK(o.title == "Mobi Test");
    CHECK(o.author == "Test Author");
    CHECK(o.lang == "en");
    CHECK(o.cover);
    CHECK(st.images == 1);
    CHECK(o.chapters.size() >= 4);   // page breaks and the size limit
    const int c2 = chapterWith(o, "Chapter Two</b>");
    CHECK(c2 > 0);
    CHECK(chapterWith(o, "Part B marker") > c2);
    CHECK(contains(o.chapters,
                   "<p style=\"text-align:center;margin-top:1em;font-size:2em;\"><span><b>The Title</b></span></p>"));
    CHECK(contains(o.chapters,
                   "<p style=\"text-indent:2em;\">First <i>chapter</i> text &amp; more &nbsp;here, 5 &amp; 6.</p>"));
    CHECK(contains(o.chapters, "<p>Unclosed before this one</p>"));
    CHECK(contains(o.chapters, "<img src=\"images/i0000\" alt=\"\" width=\"10\" height=\"12\"/>"));
    char want[80];
    snprintf(want, sizeof(want), "<a href=\"c%04d.xhtml#fp%u\">chapter two</a>", c2 + 1, ch2);
    CHECK(contains(o.chapters, want));
    snprintf(want, sizeof(want), "<a id=\"fp%u\"></a>", ch2);
    CHECK(o.chapters[c2].find(want) != std::string::npos);
    CHECK(contains(o.chapters, "href=\"http://example.com/?a=1&amp;b=2\""));
    CHECK(contains(o.chapters, "<blockquote>Quote<br/>line two<br/>three</blockquote>"));
    CHECK(contains(o.chapters, "<div class=\"center\">centered</div>"));
    CHECK(contains(o.chapters,
                   "<table><tr><td colspan=\"2\">cell</td>\n<td>next</td>\n</tr>\n<tr><td>row2</td>\n</tr>\n</table>"));
    CHECK(contains(o.chapters, "<ul><li>one</li>\n<li>two</li>\n</ul>"));
    CHECK(!contains(o.chapters, "comment"));
    // A <font> after text stays a span; the first one of a block sizes the block.
    CHECK(contains(o.chapters, "<p>Second chapter <span style=\"font-size:2em;\">big</span> text.</p>"));
    CHECK(contains(o.chapters,
                   "<p style=\"text-align:center;font-size:1.5em;\"><span>Head</span><br/><span>line two</span> "
                   "<span style=\"font-size:1.33em;\">bigger</span></p>"));
    CHECK(!contains(o.chapters, "p{a:b}"));
    CHECK(o.toc.size() == 3);
    if (o.toc.size() == 3) {
        CHECK(o.toc[0].find("|Chapter One|OEBPS/c0001.xhtml#fp") != std::string::npos);
        CHECK(o.toc[1].find("|Chapter Two|") != std::string::npos);
        CHECK(o.toc[2].find("|Part B|") != std::string::npos);
    }
    for (const auto& t : o.toc) printf("    toc %s\n", t.c_str());
    printf("  %zu chapters, %u pages, %u images on pages\n", o.chapters.size(), o.pages, o.images);
}

static void testCp1252AndRefusals() {
    printf("windows-1252, refusals\n");
    std::string epub;
    convert::ConvertStats st;
    CHECK(convertMobi(buildMobi("<html><body><p>caf\xE9 \x93quoted\x94 \x80</p></body></html>", {}, 1252), epub, st));
    Opened o = openEpub(epub);
    CHECK(o.ok && contains(o.chapters, "caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x82\xAC"));
    convert::ConvertResult r;
    CHECK(!convertMobi(buildMobi("<p>x</p>", {}, 65001, 1, 2), epub, st, &r) &&
          r == convert::ConvertResult::Unsupported);
    CHECK(st.error.find("DRM") != std::string::npos);
    CHECK(!convertMobi(buildMobi("<p>x</p>", {}, 65001, 17480), epub, st, &r) &&
          r == convert::ConvertResult::Unsupported);
    // KF8 without its part tables: unreadable
    CHECK(!convertMobi(buildMobi("<p>x</p>", {}, 65001, 2, 0, 8), epub, st, &r) &&
          r == convert::ConvertResult::ParseError);
    CHECK(!convertMobi("not a palm database at all, just text that is long enough to have a header......", epub, st,
                       &r) &&
          r == convert::ConvertResult::NotThisFormat);
    // <br> straight inside a p starts the next p (without the space above); a
    // long p goes on in another at a space.
    std::string brs = "<html><body><p height=\"1em\" width=\"2em\">one<br>two<br></br><b>x<br>y</b></p><p>";
    for (int i = 0; i < 400; i++) brs += "word" + std::to_string(i) + " ";
    brs += "</p></body></html>";
    CHECK(convertMobi(buildMobi(brs, {}, 65001), epub, st));
    o = openEpub(epub);
    CHECK(o.ok &&
          contains(o.chapters,
                   "<p style=\"text-indent:2em;margin-top:1em;\">one</p>\n<p style=\"text-indent:2em;\">two</p>\n"
                   "<p style=\"text-indent:2em;\"><b>x<br/>y</b></p>"));
    CHECK(contains(o.chapters, "</p>\n<p style=\"margin-top:0;text-indent:0;\">word") &&
          contains(o.chapters, "word399 </p>"));
    // A book with no page breaks and no contents: split by size, one entry per file.
    std::string big = "<html><body>";
    for (int i = 0; i < 3000; i++) big += "<p>Paragraph " + std::to_string(i) + " of one long run.</p>";
    big += "</body></html>";
    CHECK(convertMobi(buildMobi(big, {}, 65001), epub, st));
    o = openEpub(epub);
    CHECK(o.ok && o.chapters.size() > 1 && o.toc.size() == o.chapters.size());
    if (!o.toc.empty()) CHECK(o.toc[0].find("|1. Paragraph 0 of one long run.") != std::string::npos);
}

static void testRealBooks(const char* dir) {
    std::vector<std::string> files = filesIn(dir, ".mobi");
    for (const auto& f : filesIn(dir, ".azw3")) files.push_back(f);
    for (const auto& f : files) {
        const std::string mobi = readFile(f);
        std::string epub;
        convert::ConvertStats st;
        const clock_t t0 = clock();
        const bool ok = convertMobi(mobi, epub, st);
        const double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
        CHECK(ok);
        const Opened o = openEpub(epub);
        CHECK(o.ok);
        CHECK(o.chapters.size() == st.chapters);
        printf("  %s: %u KB -> %u KB in %.0f ms, %u chapters, %u images, %zu contents entries, %u pages, %u link targets\n",
               f.c_str(), (unsigned)(mobi.size() / 1024), (unsigned)(epub.size() / 1024), ms, st.chapters, st.images,
               o.toc.size(), o.pages, st.ids);
        for (size_t i = 0; i < o.toc.size() && i < 4; i++) printf("    toc %s\n", o.toc[i].c_str());
        if (getenv("MOBI_KEEP")) {
            const std::string out = std::string(getenv("MOBI_KEEP")) + "/" + f.substr(f.rfind('/') + 1) + ".epub";
            FILE* w = fopen(out.c_str(), "wb");
            if (w) {
                fwrite(epub.data(), 1, epub.size(), w);
                fclose(w);
            }
        }
    }
}

int main(int argc, char** argv) {
    testFixture();
    testCp1252AndRefusals();
    testRealBooks(argc > 1 ? argv[1] : "test/books");
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
