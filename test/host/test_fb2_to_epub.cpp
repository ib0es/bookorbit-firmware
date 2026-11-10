// FB2 -> EPUB conversion. Run: test/host/run.sh
// A made-up FictionBook with every element the converter handles (sections,
// titles, epigraph, poem, cite, table, notes, links, images, windows-1251) is
// converted and the EPUB opened and laid out by the engine; then every
// test/books/*.fb2 there is (not in the repository: local books) the same way.
#include "convert/fb2_to_epub.h"
#include "convert/zip_writer.h"
#include "convert_test_util.h"

#include <stdlib.h>
#include <time.h>

static bool convertString(const std::string& fb2, std::string& epub, convert::ConvertStats& st,
                          convert::ConvertResult* result = nullptr) {
    MemIn in(fb2);
    MemOut out;
    MemTemp tmp;
    const convert::ConvertResult r = convert::fb2ToEpub(in, out, tmp, nullptr, nullptr, &st);
    if (result) *result = r;
    epub = out.d;
    if (const char* dump = getenv("FB2_DUMP")) {   // keep the EPUB (and its CFI map) to look at
        static int n = 0;
        const std::string base = std::string(dump) + "/fb2-" + std::to_string(++n);
        if (FILE* f = fopen((base + ".epub").c_str(), "wb")) { fwrite(epub.data(), 1, epub.size(), f); fclose(f); }
        if (FILE* f = fopen((base + ".cfimap").c_str(), "wb")) { fwrite(st.cfiMap.data(), 1, st.cfiMap.size(), f); fclose(f); }
    }
    if (r != convert::ConvertResult::Ok) printf("  convert: %s %s\n", convert::resultName(r), st.error.c_str());
    return r == convert::ConvertResult::Ok;
}

// ---------------------------------------------------------------- fixtures

// 1x1 PNG
static const char* kPng =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAACklEQVR4nGNgAAAAAgABSK+kcQAAAABJRU5ErkJggg==";

static std::string fixture() {
    std::string big;
    for (int i = 0; i < 1200; i++) big += "<p>Long chapter paragraph number " + std::to_string(i) + " with some words in it.</p>\n";
    return std::string(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" xmlns:l=\"http://www.w3.org/1999/xlink\">\n"
        "<description><title-info><genre>prose</genre>"
        "<author><first-name>Ivan</first-name><middle-name>I.</middle-name><last-name>Testov</last-name></author>"
        "<author><nickname>Anon</nickname></author>"
        "<book-title>Test &amp; Book</book-title>"
        "<annotation><p>First line.</p><p>Second line.</p></annotation>"
        "<coverpage><image l:href=\"#cover.png\"/></coverpage>"
        "<lang>en</lang><sequence name=\"Tests\" number=\"3\"/></title-info>"
        "<document-info><author><first-name>Not</first-name><last-name>Me</last-name></author></document-info>"
        "</description>\n"
        "<body><title><p>Test Book</p><p>Subtitle line</p></title>"
        "<epigraph><p>An epigraph.</p><text-author>Somebody</text-author></epigraph>"
        "<section id=\"ch1\"><title><p>Chapter One</p></title>"
        "<p>Text with <emphasis>em</emphasis>, <strong>strong</strong>, <strikethrough>gone</strikethrough>, "
        "x<sup>2</sup>, H<sub>2</sub>O, <code>code()</code> and a note<a l:href=\"#n1\" type=\"note\">[1]</a>.</p>"
        "<empty-line/>"
        "<p>Link to <a l:href=\"#ch2\">chapter two</a> and <a l:href=\"https://example.com/?a=1&amp;b=2\">out</a>.</p>"
        "<image l:href=\"#pic.png\"/>"
        "<p>Inline <image l:href=\"#pic.png\"/> image &lt;tag&gt;.</p>"
        "<poem><title><p>Poem title</p></title><stanza><v>Line one</v><v>Line two</v></stanza>"
        "<stanza><v>Line three</v></stanza><text-author>Poet</text-author><date>1900</date></poem>"
        "<cite><p>Cited.</p><text-author>Citer</text-author></cite>"
        "<table><tr><th>A</th><th>B</th></tr><tr><td colspan=\"2\">both</td></tr></table>"
        "<subtitle>A subtitle</subtitle>"
        "<section id=\"ch1-1\"><title><p>Part 1.1</p></title><p id=\"p11\">Inside part.</p></section>"
        "</section>\n"
        "<section id=\"ch2\"><title><p>Chapter Two</p></title><p>Second chapter.</p></section>\n"
        "<section><title><p>Long Chapter</p></title>") +
        big +
        "</section>\n"
        "<section id=\"empty\"/>\n"
        "</body>\n"
        "<body name=\"notes\"><title><p>Notes</p></title>"
        "<section id=\"n1\"><title><p>1</p></title><p>The note text, back to <a l:href=\"#p11\">part</a>.</p></section>"
        "</body>\n"
        "<binary id=\"cover.png\" content-type=\"image/png\">" + kPng + "</binary>\n"
        "<binary id=\"pic.png\" content-type=\"image/png\">\n" + std::string(kPng).substr(0, 40) + "\n" +
        std::string(kPng).substr(40) + "\n</binary>\n"
        "</FictionBook>\n";
}

static void testFixture() {
    printf("fixture\n");
    std::string epub;
    convert::ConvertStats st;
    CHECK(convertString(fixture(), epub, st));
    const Opened o = openEpub(epub);
    CHECK(o.ok);
    CHECK(o.title == "Test & Book");
    CHECK(o.author.find("Ivan I. Testov") != std::string::npos);
    CHECK(o.lang == "en");
    CHECK(o.cover);
    CHECK(st.images == 2);
    // Chapters: the top-level sections, the long one split by size, the notes apart.
    CHECK(o.chapters.size() >= 5);
    const int c1 = chapterWith(o, "Chapter One"), c2 = chapterWith(o, "Second chapter.");
    const int notes = chapterWith(o, "The note text");
    CHECK(c1 >= 0 && c2 > c1 && notes > c2);
    CHECK(chapterWith(o, "paragraph number 0 ") != chapterWith(o, "paragraph number 1199 "));
    // Laid out as the web reader's foliate-js lays out FB2 (CFIs mean the same place):
    // a title is a block with a heading per line, sections stay sections.
    CHECK(contains(o.chapters, "<div class=\"title\"><h1>Test Book</h1><h1>Subtitle line</h1>"));
    CHECK(contains(o.chapters, "<section id=\"ch1\"><div class=\"title\"><h2>Chapter One</h2>"));
    CHECK(contains(o.chapters, "<section id=\"ch1-1\"><div class=\"title\"><h3>Part 1.1</h3>"));
    CHECK(contains(o.chapters, "<em>em</em>"));
    CHECK(contains(o.chapters, "<strong>strong</strong>"));
    CHECK(contains(o.chapters, "<del>gone</del>"));
    CHECK(contains(o.chapters, "<sup>2</sup>"));
    CHECK(contains(o.chapters, "<code>code()</code>"));
    CHECK(contains(o.chapters, "&lt;tag&gt;"));
    CHECK(contains(o.chapters, "<blockquote class=\"epigraph\">"));
    CHECK(contains(o.chapters, "<p class=\"text-author\">Somebody</p>"));
    // A poem's title is not shown (foliate drops it); a stanza is one block, its lines after <br/>.
    CHECK(contains(o.chapters, "<blockquote class=\"poem\"><p class=\"stanza\">Line one<br/>Line two<br/></p>"));
    CHECK(!contains(o.chapters, "Poem title"));
    CHECK(contains(o.chapters, "<td colspan=\"2\">both</td>"));
    CHECK(contains(o.chapters, "<p class=\"subtitle\">A subtitle</p>"));
    CHECK(contains(o.chapters, "<p class=\"empty\">&#160;</p>"));
    CHECK(contains(o.chapters, "<div class=\"image\"><img src=\"images/i0001\" alt=\"\"/></div>"));
    CHECK(contains(o.chapters, "Inline <img src=\"images/i0001\" alt=\"\"/> image"));
    // Links: across files to the file holding the id; within a file bare.
    char want[64];
    snprintf(want, sizeof(want), "href=\"c%04d.xhtml#n1\">[1]</a>", notes + 1);
    CHECK(contains(o.chapters, want));
    snprintf(want, sizeof(want), "href=\"c%04d.xhtml#ch2\">chapter two</a>", c2 + 1);
    CHECK(contains(o.chapters, want));
    snprintf(want, sizeof(want), "href=\"c%04d.xhtml#p11\">part</a>", chapterWith(o, "id=\"p11\"") + 1);
    CHECK(contains(o.chapters, want));
    CHECK(contains(o.chapters, "href=\"https://example.com/?a=1&amp;b=2\""));
    CHECK(contains(o.chapters, "class=\"note\""));
    // Contents: chapters with sub-entries, then the notes.
    CHECK(o.toc.size() == 5);
    if (o.toc.size() == 5) {
        CHECK(o.toc[0].find("0|Chapter One|") == 0 && o.toc[0].find("#ch1") != std::string::npos);
        CHECK(o.toc[1].find("1|Part 1.1|") == 0);
        CHECK(o.toc[2].find("0|Chapter Two|") == 0);
        CHECK(o.toc[3].find("0|Long Chapter|") == 0);
        CHECK(o.toc[4].find("0|Notes|") == 0);
    }
    for (const auto& t : o.toc) printf("    toc %s\n", t.c_str());
    // The images are the decoded bytes.
    MemSource src(epub);
    ZipCatalog zip;
    Arena a(g_scratch, sizeof(g_scratch));
    CHECK(zip.open(src, a) == BookStatus::Ok);
    const ZipEntry* img = zip.find("OEBPS/images/i0001");
    CHECK(img && img->uncompressedSize == 67);
    printf("  %zu chapters, %u pages, %u images on pages\n", o.chapters.size(), o.pages, o.images);
}

static void testCp1251() {
    printf("windows-1251\n");
    // "Привет, мир" in windows-1251 with a title "Глава"
    const std::string fb2 =
        "<?xml version=\"1.0\" encoding=\"windows-1251\"?>"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\"><description><title-info>"
        "<book-title>\xCA\xED\xE8\xE3\xE0</book-title><lang>ru</lang></title-info></description>"
        "<body><section><title><p>\xC3\xEB\xE0\xE2\xE0</p></title><p>\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0 \xB9 \xAB\xBB</p></section></body>"
        "</FictionBook>";
    std::string epub;
    convert::ConvertStats st;
    CHECK(convertString(fb2, epub, st));
    const Opened o = openEpub(epub);
    CHECK(o.ok);
    CHECK(o.title == "\xD0\x9A\xD0\xBD\xD0\xB8\xD0\xB3\xD0\xB0");   // Книга
    CHECK(contains(o.chapters, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, \xD0\xBC\xD0\xB8\xD1\x80 "
                               "\xE2\x84\x96 \xC2\xAB\xC2\xBB"));   // Привет, мир № «»
    CHECK(o.toc.size() == 1 && o.toc[0].find("\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0") != std::string::npos);
}

static void testEdges() {
    printf("edges\n");
    std::string epub;
    convert::ConvertStats st;
    convert::ConvertResult r;
    CHECK(!convertString("<html><body>not a book</body></html>", epub, st, &r) &&
          r == convert::ConvertResult::NotThisFormat);
    CHECK(!convertString("<FictionBook><body><p>broken</body></FictionBook>", epub, st, &r) &&
          r == convert::ConvertResult::ParseError);
    // A book without text still makes a valid EPUB.
    CHECK(convertString("<FictionBook><description/><body/></FictionBook>", epub, st));
    CHECK(openEpub(epub).ok);
    // Untitled sections: several -> named by their first short line; one -> the book title.
    CHECK(convertString("<FictionBook><description><title-info><book-title>B</book-title></title-info></description>"
                        "<body><section><p>Contents</p><p>CHAPTER I.</p></section><section><p>CHAPTER I.</p><p>Down the Hole</p></section><section><empty-line/><p>CHAPTER II.</p></section>"
                        "<section><p>" + std::string(200, 'y') + "</p></section></body></FictionBook>", epub, st));
    Opened o = openEpub(epub);
    CHECK(o.toc.size() == 4 && o.toc[0].find("|Contents|") != std::string::npos &&
          o.toc[1].find("|CHAPTER I. Down the Hole|") != std::string::npos &&
          o.toc[2].find("|CHAPTER II.|") != std::string::npos && o.toc[3].find("|4|") != std::string::npos);
    for (const auto& t : o.toc) printf("    toc %s\n", t.c_str());
    CHECK(convertString("<FictionBook><description><title-info><book-title>B</book-title></title-info></description>"
                        "<body><section><p>only</p></section></body></FictionBook>", epub, st));
    o = openEpub(epub);
    CHECK(o.toc.size() == 1 && o.toc[0].find("|B|") != std::string::npos);
    // One untitled section split by size: an entry per file, by its first words.
    std::string one = "<FictionBook><body><section><p>First words of the book that go on for a while here</p>";
    for (int i = 0; i < 3000; i++) one += "<p>Paragraph " + std::to_string(i) + " of the one long section.</p>";
    one += "</section></body></FictionBook>";
    CHECK(convertString(one, epub, st));
    o = openEpub(epub);
    CHECK(o.chapters.size() > 1 && o.toc.size() == o.chapters.size());
    if (o.toc.size() > 1) {
        CHECK(o.toc[0].find("|1. First words of the book that go on for a while here\xE2\x80\xA6|") != std::string::npos);
        CHECK(o.toc[1].find("|2. Paragraph ") != std::string::npos);
        printf("    toc %s\n    toc %s\n", o.toc[0].c_str(), o.toc[1].c_str());
    }
    // calibre-style spacing: an empty-line after every paragraph is dropped, two in a row stay one.
    std::string sp = "<FictionBook><body><section>";
    for (int i = 0; i < 10; i++) sp += "<p>para</p><empty-line/>";
    sp += "<empty-line/><p>after</p></section></body></FictionBook>";
    CHECK(convertString(sp, epub, st));
    o = openEpub(epub);
    size_t blanks = 0;
    for (size_t at = 0; (at = o.chapters[0].find("class=\"empty\"", at)) != std::string::npos; at++) blanks++;
    CHECK(blanks == 1);
    // A paragraph past ~2.4 KB goes on in another, cut at a space.
    std::string lp = "<FictionBook><body><section><p>";
    for (int i = 0; i < 400; i++) lp += "word" + std::to_string(i) + " ";
    lp += "</p></section></body></FictionBook>";
    CHECK(convertString(lp, epub, st));
    o = openEpub(epub);
    CHECK(o.ok && contains(o.chapters, "<p class=\"cont\">") && contains(o.chapters, "word399 </p>"));
    // The pieces stand in one element, as foliate's one paragraph, and the map says where each starts.
    CHECK(contains(o.chapters, "<div class=\"lp\"><p>word0 "));
    CHECK(st.cfiMap.find("\nL 1 4/2/2 0 0 0 ") != std::string::npos);
    if (!o.chapters.empty()) {
        const std::string& c = o.chapters[0];
        const size_t at = c.find("</p><p class=\"cont\">");
        CHECK(at != std::string::npos && at > 0 && c[at - 1] == ' ');
    }
}

static void testRealBooks(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) {
        printf("real books: %s missing, skipped\n", dir);
        return;
    }
    std::vector<std::string> files;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".fb2") == 0) files.push_back(std::string(dir) + "/" + n);
    }
    closedir(d);
    for (const auto& f : files) {
        const std::string fb2 = readFile(f);
        std::string epub;
        convert::ConvertStats st;
        const clock_t t0 = clock();
        const bool ok = convertString(fb2, epub, st);
        const double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
        CHECK(ok);
        const Opened o = openEpub(epub);
        CHECK(o.ok);
        CHECK(o.chapters.size() == st.chapters);
        printf("  %s: %u KB -> %u KB in %.0f ms, %u chapters, %u images, %zu contents entries, %u pages, %u ids\n",
               f.c_str(), (unsigned)(fb2.size() / 1024), (unsigned)(epub.size() / 1024), ms, st.chapters, st.images,
               o.toc.size(), o.pages, st.ids);
        if (getenv("FB2_KEEP")) {
            const std::string out = std::string(getenv("FB2_KEEP")) + "/" + f.substr(f.rfind('/') + 1) + ".epub";
            FILE* w = fopen(out.c_str(), "wb");
            if (w) { fwrite(epub.data(), 1, epub.size(), w); fclose(w); }
        }
    }
}

int main(int argc, char** argv) {
    testFixture();
    testCp1251();
    testEdges();
    testRealBooks(argc > 1 ? argv[1] : "test/books");
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
