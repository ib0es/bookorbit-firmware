// TXT -> EPUB conversion. Run: test/host/run.sh
// Made-up texts in each encoding and paragraph style are converted and the
// EPUB opened and laid out by the engine; then every test/books/*.txt there
// is (local books, not in the repository).
#include "convert/txt_to_epub.h"
#include "convert_test_util.h"

#include <time.h>

static std::string convertTxt(const std::string& txt, convert::ConvertStats* st = nullptr,
                              convert::ConvertResult* res = nullptr) {
    MemIn in(txt);
    MemOut out;
    convert::ConvertStats local;
    const convert::ConvertResult r = convert::txtToEpub(in, out, "Test Book", nullptr, nullptr, st ? st : &local);
    if (res) *res = r;
    return r == convert::ConvertResult::Ok ? out.d : std::string();
}

// Code points of UTF-8 text, for re-encoding.
static std::vector<uint32_t> codepoints(const std::string& s) {
    std::vector<uint32_t> o;
    for (size_t i = 0; i < s.size();) {
        const uint8_t b = (uint8_t)s[i];
        uint32_t c;
        int n;
        if (b < 0x80)
            c = b, n = 1;
        else if (b < 0xE0)
            c = b & 0x1F, n = 2;
        else if (b < 0xF0)
            c = b & 0x0F, n = 3;
        else
            c = b & 0x07, n = 4;
        for (int k = 1; k < n; k++) c = (c << 6) | ((uint8_t)s[i + k] & 0x3F);
        o.push_back(c);
        i += n;
    }
    return o;
}

// UTF-8 -> a single-byte code page, by a table of its upper half.
static std::string toCodePage(const std::string& s, const char* python) {
    // the tables, as Python's codecs give them (0x80..0xFF)
    static const struct {
        const char* name;
        uint16_t t[128];
    } pages[] = {
            {"cp1251",
             {0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
              0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122,
              0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
              0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
              0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410,
              0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
              0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
              0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
              0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
              0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F}},
            {"koi8_r",
             {0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2580, 0x2584,
              0x2588, 0x258C, 0x2590, 0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248, 0x2264, 0x2265,
              0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7, 0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555,
              0x2556, 0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E, 0x255F, 0x2560, 0x2561, 0x0401,
              0x2562, 0x2563, 0x2564, 0x2565, 0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9, 0x044E,
              0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, 0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C,
              0x043D, 0x043E, 0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432, 0x044C, 0x044B, 0x0437,
              0x0448, 0x044D, 0x0449, 0x0447, 0x044A, 0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
              0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x042F, 0x0420, 0x0421, 0x0422,
              0x0423, 0x0416, 0x0412, 0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A}},
            {"cp866",
             {0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C,
              0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429,
              0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436,
              0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x2591, 0x2592, 0x2593, 0x2502,
              0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514,
              0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
              0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C,
              0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
              0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F, 0x0401, 0x0451, 0x0404, 0x0454, 0x0407,
              0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0}},
    };
    const uint16_t* t = nullptr;
    for (const auto& p : pages)
        if (strcmp(p.name, python) == 0) t = p.t;
    std::string o;
    for (uint32_t c : codepoints(s)) {
        if (c < 0x80) {
            o += (char)c;
            continue;
        }
        char b = c == 0x2014 ? '-' : '?';   // KOI8-R and CP866 have no dash
        for (int i = 0; i < 128; i++)
            if (t[i] == c) b = (char)(0x80 + i);
        o += b;
    }
    return o;
}

static std::string toUtf16(const std::string& s, bool le, bool bom) {
    std::string o;
    auto unit = [&](uint16_t u) {
        if (le)
            o += (char)(u & 0xFF), o += (char)(u >> 8);
        else
            o += (char)(u >> 8), o += (char)(u & 0xFF);
    };
    if (bom) unit(0xFEFF);
    for (uint32_t c : codepoints(s)) {
        if (c >= 0x10000) {
            unit((uint16_t)(0xD800 + ((c - 0x10000) >> 10)));
            unit((uint16_t)(0xDC00 + ((c - 0x10000) & 0x3FF)));
        } else {
            unit((uint16_t)c);
        }
    }
    return o;
}

static std::string words(int n, int seed) {
    static const char* const w[] = {"мама",   "мыла", "раму", "долго", "и",     "старательно", "потом", "пошла",
                                    "гулять", "в",    "парк", "где",   "росли", "липы",        "клёны", "Ёлки"};
    std::string s;
    for (int i = 0; i < n; i++) {
        if (i) s += ' ';
        s += w[(i * 7 + seed) % 16];
    }
    return s + ".";
}

static std::string wordsEn(int n, int seed) {
    static const char* const w[] = {"alice",   "was", "beginning", "to",     "get", "very", "tired", "of",
                                    "sitting", "by",  "her",       "sister", "on",  "the",  "bank",  "and"};
    std::string s;
    for (int i = 0; i < n; i++) {
        if (i) s += ' ';
        s += w[(i * 5 + seed) % 16];
    }
    return s + ".";
}

// Hard-wraps a paragraph at `width` columns (by bytes; fine for ASCII).
static std::string wrap(const std::string& p, size_t width, const std::string& indent = "") {
    std::string o, line = indent;
    size_t i = 0;
    while (i < p.size()) {
        size_t j = p.find(' ', i);
        if (j == std::string::npos) j = p.size();
        const std::string w = p.substr(i, j - i);
        if (line.size() > indent.size() && line.size() + 1 + w.size() > width) {
            o += line + "\r\n";
            line.clear();
        }
        if (!line.empty() && line != indent) line += ' ';
        line += w;
        i = j + 1;
    }
    return o + line + "\r\n";
}

static std::string tocText(const Opened& o) {
    std::string s;
    for (const auto& t : o.toc) s += "    toc " + t + "\n";
    return s;
}

// Russian, a paragraph per line, with a contents list, parts and named chapters.
static std::string russianBook() {
    std::string t;
    t += "Тестовая книга\n\nСодержание\nГлава 1\nГлава 2\nГлава 3\n\n";
    t += "Посвящается всем, кто дочитал до конца.\n\n";
    t += "Часть первая\n\nГлава 1\nНачало\n\n";
    for (int i = 0; i < 30; i++) t += "\t" + words(40, i) + "\n";
    t += "* * *\n";
    t += "Том два раза заходил в парк и смотрел на липы.\n";
    t += "Глава семьи сказал, что пора домой.\n";
    for (int i = 0; i < 30; i++) t += "  " + words(35, i + 3) + "\n";
    t += "\nГлава 2\n— Ты кто такой? — спросил он.\n";
    for (int i = 0; i < 40; i++) t += "   " + words(30, i + 5) + "\n";
    t += "\nЧасть вторая. Возвращение\nГлава 3. Конец\n";
    for (int i = 0; i < 40; i++) t += words(30, i + 9) + "\n";
    t += "Эпилог\n";
    for (int i = 0; i < 10; i++) t += words(30, i + 11) + "\n";
    return t;
}

static void checkRussian(const char* what, const std::string& txt) {
    printf("  %s\n", what);
    convert::ConvertStats st;
    const std::string epub = convertTxt(txt, &st);
    CHECK(!epub.empty());
    const Opened o = openEpub(epub);
    CHECK(o.ok);
    CHECK(o.lang == "ru");
    CHECK(o.title == "Test Book");
    // the contents list stays text; the heading after a part goes in the part's file
    std::vector<std::string> want = {"0|Тестовая книга Содержание Глава 1 Глава 2 Глава…",
                                     "0|Часть первая",
                                     "1|Глава 1. Начало",
                                     "1|Глава 2",
                                     "0|Часть вторая. Возвращение",
                                     "1|Глава 3. Конец",
                                     "1|Эпилог"};
    CHECK(o.toc.size() == want.size());
    for (size_t i = 0; i < want.size() && i < o.toc.size(); i++) {
        const std::string got = o.toc[i].substr(0, o.toc[i].find('|', 2));
        if (got != want[i]) printf("    toc %zu: %s, want %s\n", i, got.c_str(), want[i].c_str());
        CHECK(got == want[i]);
    }
    CHECK(contains(o.chapters, "<h1 id=\"h1\">Часть первая</h1>\n<h2 id=\"h2\">Глава 1<br/>Начало</h2>"));
    CHECK(contains(o.chapters, "<p>Глава 1</p>"));   // the contents list
    CHECK(contains(o.chapters, "<p>Том два раза заходил в парк и смотрел на липы.</p>"));
    CHECK(contains(o.chapters, "<p>Глава семьи сказал, что пора домой.</p>"));
    CHECK(contains(o.chapters, "<p class=\"sep\">* * *</p>"));
    CHECK(contains(o.chapters, "<p>— Ты кто такой? — спросил он.</p>") ||
          contains(o.chapters, "<p>- Ты кто такой? - спросил он.</p>"));   // not taken for the chapter's name
    CHECK(contains(o.chapters, "Ёлки"));
    CHECK(chapterWith(o, "Часть первая") != chapterWith(o, "Посвящается"));
    CHECK(chapterWith(o, "Часть первая") == chapterWith(o, "Глава 1<br/>Начало"));
    CHECK(chapterWith(o, "<h2 id=\"h3\">Глава 2") > chapterWith(o, "Глава 1<br/>Начало"));
    CHECK(o.pages > 5);
}

static void testRussian() {
    const std::string utf8 = russianBook();
    checkRussian("UTF-8", utf8);
    checkRussian("UTF-8 with a BOM", "\xEF\xBB\xBF" + utf8);
    checkRussian("windows-1251", toCodePage(utf8, "cp1251"));
    checkRussian("KOI8-R", toCodePage(utf8, "koi8_r"));
    checkRussian("CP866", toCodePage(utf8, "cp866"));
    checkRussian("UTF-16LE", toUtf16(utf8, true, true));
    checkRussian("UTF-16BE", toUtf16(utf8, false, true));
    checkRussian("UTF-16LE with no BOM", toUtf16(utf8, true, false));
    uint32_t bom = 0;
    const std::string cp = toCodePage(utf8, "cp1251");
    CHECK(convert::detectEncoding((const uint8_t*)cp.data(), (uint32_t)cp.size(), &bom) ==
          convert::TextEncoding::Cp1251);
    // Mac line ends
    std::string mac = utf8;
    for (char& c : mac)
        if (c == '\n') c = '\r';
    checkRussian("CR line ends", mac);
}

// Project Gutenberg style: hard-wrapped, blank lines between paragraphs.
static void testGutenberg() {
    printf("  hard-wrapped English\n");
    std::string t = "The Project Gutenberg eBook of Test\r\n\r\n";
    t += "Contents\r\n\r\n CHAPTER I.     Down the Rabbit-Hole\r\n CHAPTER II.    The Pool of Tears\r\n\r\n\r\n";
    const char* names[] = {"Down the Rabbit-Hole", "The Pool of Tears"};
    for (int c = 0; c < 2; c++) {
        t += std::string("CHAPTER ") + (c ? "II." : "I.") + "\r\n" + names[c] + "\r\n\r\n\r\n";
        for (int i = 0; i < 25; i++) t += wrap(wordsEn(60, i + c), 70) + "\r\n";
        t += "Book I read was long.\r\n\r\n";
    }
    convert::ConvertStats st;
    const std::string epub = convertTxt(t, &st);
    const Opened o = openEpub(epub);
    CHECK(o.ok);
    CHECK(o.lang == "en");
    printf("%s", tocText(o).c_str());
    CHECK(o.toc.size() == 3);
    CHECK(contains(o.toc, "0|CHAPTER I. Down the Rabbit-Hole|"));
    CHECK(contains(o.toc, "0|CHAPTER II. The Pool of Tears|"));
    CHECK(contains(o.chapters, "<h2 id=\"h1\">CHAPTER I.<br/>Down the Rabbit-Hole</h2>"));
    // wrapped lines joined into paragraphs
    const std::string p0 = "<p>" + wordsEn(60, 0) + "</p>";
    CHECK(contains(o.chapters, p0));
    CHECK(contains(o.chapters, "<p>Book I read was long.</p>"));
    CHECK(st.error.find("blank-line") != std::string::npos);
}

// Hard-wrapped with an indent starting each paragraph, and 1984's lone numbers.
static void testIndented() {
    printf("  indented, roman numbers\n");
    std::string t;
    for (int c = 0; c < 3; c++) {
        t += std::string(c == 0 ? "I" : c == 1 ? "II" : "III") + "\n";
        for (int i = 0; i < 20; i++) t += wrap(words(50, i + c), 72, "    ");
    }
    convert::ConvertStats st;
    const Opened o = openEpub(convertTxt(t, &st));
    CHECK(o.ok);
    printf("%s    %s\n", tocText(o).c_str(), st.error.c_str());
    if (o.toc.size() != 3 && !o.chapters.empty()) printf("%s\n", o.chapters[0].substr(0, 1500).c_str());
    CHECK(o.toc.size() == 3);
    CHECK(contains(o.toc, "0|II|"));
    CHECK(contains(o.chapters, "<p>" + words(50, 0) + "</p>"));
    CHECK(st.error.find("indented") != std::string::npos);
}

// One endless line: cut into paragraphs at sentence ends, files of ~40 KB.
static void testLongLine() {
    printf("  one long line\n");
    std::string t;
    while (t.size() < 200000) t += words(23, (int)t.size()) + " ";
    const Opened o = openEpub(convertTxt(t));
    CHECK(o.ok);
    CHECK(o.chapters.size() >= 4);
    size_t longest = 0, pars = 0;
    bool cutMidWord = false;
    for (const std::string& c : o.chapters) {
        for (size_t at = 0; (at = c.find("<p", at)) != std::string::npos; at++) {
            const size_t b = c.find('>', at) + 1, e = c.find("</p>", b);
            longest = std::max(longest, e - b);
            pars++;
            if (e > b && c[e - 1] != '.') cutMidWord = true;   // every cut at a sentence's end
        }
    }
    CHECK(longest <= 2400);
    CHECK(pars > 60);
    CHECK(!cutMidWord);
    CHECK(o.toc.size() == o.chapters.size());   // first words of each file
}

static void testOdd() {
    printf("  empty, binary junk, mixed encodings\n");
    convert::ConvertResult r;
    Opened o = openEpub(convertTxt("", nullptr, &r));
    CHECK(r == convert::ConvertResult::Ok);
    CHECK(o.ok && o.chapters.size() == 1);
    o = openEpub(convertTxt(std::string("a\0b\x01<c>&d\x7f", 10) + "\n", nullptr, &r));
    CHECK(o.ok);
    CHECK(contains(o.chapters, "<p>ab&lt;c&gt;&amp;d</p>"));
    // ASCII for the first 64 KB, then windows-1251: the stray bytes still read as Cyrillic
    std::string t;
    while (t.size() < 70000) t += wordsEn(20, (int)t.size()) + "\n";
    t += toCodePage("Привет, мир.\n", "cp1251");
    o = openEpub(convertTxt(t));
    CHECK(o.ok);
    CHECK(contains(o.chapters, "<p>Привет, мир.</p>"));
    // windows-1252
    o = openEpub(convertTxt("Caf\xE9 cr\xE8me \x93quoted\x94\n"));
    CHECK(contains(o.chapters, "<p>Café crème “quoted”</p>"));
}

static void testRealBooks(const char* dir) {
    for (const std::string& path : filesIn(dir, ".txt")) {
        const std::string data = readFile(path);
        convert::ConvertStats st;
        const clock_t t0 = clock();
        convert::ConvertResult r;
        const std::string epub = convertTxt(data, &st, &r);
        const double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        printf("  %s: %s (%s), %u KB -> %u KB, %u files, %u contents entries, %.0f ms\n", path.c_str(),
               convert::resultName(r), st.error.c_str(), (unsigned)(data.size() / 1024), (unsigned)(epub.size() / 1024),
               st.chapters, st.tocEntries, ms);
        CHECK(r == convert::ConvertResult::Ok);
        const Opened o = openEpub(epub);
        CHECK(o.ok);
        printf("    %u pages, lang %s\n", o.pages, o.lang.c_str());
        for (size_t i = 0; i < o.toc.size() && i < 12; i++) printf("    toc %s\n", o.toc[i].c_str());
        if (const char* keep = getenv("TXT_KEEP")) {
            const std::string out = std::string(keep) + "/" + path.substr(path.rfind('/') + 1) + ".epub";
            if (FILE* f = fopen(out.c_str(), "wb")) {
                fwrite(epub.data(), 1, epub.size(), f);
                fclose(f);
            }
        }
    }
}

int main(int argc, char** argv) {
    testRussian();
    testGutenberg();
    testIndented();
    testLongLine();
    testOdd();
    testRealBooks(argc > 1 ? argv[1] : "test/books");
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
