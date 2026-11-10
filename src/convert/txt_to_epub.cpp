#include "txt_to_epub.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <vector>

#include "zip_writer.h"

namespace convert {

const char* encodingName(TextEncoding e) {
    switch (e) {
        case TextEncoding::Utf8: return "UTF-8";
        case TextEncoding::Utf16LE: return "UTF-16LE";
        case TextEncoding::Utf16BE: return "UTF-16BE";
        case TextEncoding::Cp1251: return "windows-1251";
        case TextEncoding::Koi8R: return "KOI8-R";
        case TextEncoding::Cp866: return "CP866";
        case TextEncoding::Cp1252: return "windows-1252";
    }
    return "?";
}

namespace {

// ------------------------------------------------------------ code pages (0x80..0xFF)

const uint16_t kCp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C,
    0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A,
    0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9,
    0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415,
    0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423,
    0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431,
    0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D,
    0x044E, 0x044F};

const uint16_t kKoi8R[128] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2580, 0x2584, 0x2588,
    0x258C, 0x2590, 0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248, 0x2264, 0x2265, 0x00A0, 0x2321,
    0x00B0, 0x00B2, 0x00B7, 0x00F7, 0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555, 0x2556, 0x2557, 0x2558,
    0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E, 0x255F, 0x2560, 0x2561, 0x0401, 0x2562, 0x2563, 0x2564, 0x2565,
    0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9, 0x044E, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435,
    0x0444, 0x0433, 0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x044F, 0x0440, 0x0441,
    0x0442, 0x0443, 0x0436, 0x0432, 0x044C, 0x044B, 0x0437, 0x0448, 0x044D, 0x0449, 0x0447, 0x044A, 0x042E, 0x0410,
    0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, 0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E,
    0x041F, 0x042F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412, 0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429,
    0x0427, 0x042A};

const uint16_t kCp866[128] = {
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
    0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B,
    0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439,
    0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C,
    0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
    0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x0440, 0x0441,
    0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
    0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4,
    0x25A0, 0x00A0};

const uint16_t kCp1252[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                              0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                              0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

uint32_t singleByte(TextEncoding e, uint8_t b) {
    if (b < 0x80) return b;
    switch (e) {
        case TextEncoding::Cp1251: return kCp1251[b - 0x80];
        case TextEncoding::Koi8R: return kKoi8R[b - 0x80];
        case TextEncoding::Cp866: return kCp866[b - 0x80];
        default: return b < 0xA0 ? kCp1252[b - 0x80] : b;
    }
}

bool isCyrLower(uint32_t c) { return (c >= 0x430 && c <= 0x44F) || c == 0x451; }

// Length of the valid UTF-8 sequence at p (0: invalid, -1: cut off by the end).
int utf8Len(const uint8_t* p, uint32_t avail, uint32_t* cp) {
    const uint8_t b = p[0];
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    int n;
    uint32_t c;
    if (b >= 0xC2 && b <= 0xDF) n = 2, c = b & 0x1F;
    else if (b >= 0xE0 && b <= 0xEF) n = 3, c = b & 0x0F;
    else if (b >= 0xF0 && b <= 0xF4) n = 4, c = b & 0x07;
    else return 0;
    if (avail < (uint32_t)n) {
        for (uint32_t i = 1; i < avail; i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
        return -1;
    }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (p[i] & 0x3F);
    }
    if ((n == 3 && c < 0x800) || (n == 4 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    *cp = c;
    return n;
}

void putUtf8(std::string& o, uint32_t c) {
    if (c < 0x80) {
        o += (char)c;
    } else if (c < 0x800) {
        o += (char)(0xC0 | (c >> 6));
        o += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        o += (char)(0xE0 | (c >> 12));
        o += (char)(0x80 | ((c >> 6) & 0x3F));
        o += (char)(0x80 | (c & 0x3F));
    } else {
        o += (char)(0xF0 | (c >> 18));
        o += (char)(0x80 | ((c >> 12) & 0x3F));
        o += (char)(0x80 | ((c >> 6) & 0x3F));
        o += (char)(0x80 | (c & 0x3F));
    }
}

std::string xmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// Lowercase (ASCII and Russian letters) of UTF-8 text.
std::string lowerUtf8(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        const uint8_t c = (uint8_t)s[i];
        if (c >= 'A' && c <= 'Z') {
            o += (char)(c + 32);
        } else if (c == 0xD0 && i + 1 < s.size()) {
            const uint8_t d = (uint8_t)s[++i];
            if (d >= 0x90 && d <= 0x9F) o += "\xD0", o += (char)(d + 0x20);           // А..П
            else if (d >= 0xA0 && d <= 0xAF) o += "\xD1", o += (char)(d - 0x20);      // Р..Я
            else if (d == 0x81) o += "\xD1\x91";                                      // Ё
            else o += "\xD0", o += (char)d;
        } else {
            o += (char)c;
        }
    }
    return o;
}

size_t cpCount(const std::string& s) {
    size_t n = 0;
    for (char c : s)
        if (((uint8_t)c & 0xC0) != 0x80) n++;
    return n;
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }

const char* const kCss =
    "body{margin:0}\n"
    "p{margin:0;text-indent:1.5em;text-align:justify}\n"
    "h1,h2{text-align:center;text-indent:0;margin:1em 0 0.7em 0;font-weight:bold}\n"
    "h1{font-size:1.4em}h2{font-size:1.2em}\n"
    "p.cont{text-indent:0}\n"
    "p.sep{text-align:center;text-indent:0;margin:0.5em 0}\n";

// ------------------------------------------------------------ headings

enum Kind : uint8_t { K_TEXT, K_PART, K_CHAPTER };

bool isRoman(const std::string& t) {   // lowercase
    if (t.empty() || t.size() > 8) return false;
    for (char c : t)
        if (!strchr("ivxlcdm", c)) return false;
    return true;
}

bool isNumberWord(const std::string& t) {   // lowercase, punctuation stripped
    if (t.empty()) return false;
    bool digits = true;
    for (char c : t) digits = digits && c >= '0' && c <= '9';
    if (digits) return t.size() <= 4;
    if (isRoman(t)) return true;
    static const char* const en[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                     "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
                                     "eighteen", "nineteen", "twenty", "thirty", "forty", "fifty", "first", "second",
                                     "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth",
                                     "last"};
    for (const char* w : en)
        if (t == w || (strlen(w) > 3 && startsWith(t, w) && t.size() > strlen(w) && t[strlen(w)] == '-')) return true;
    // Russian: ordinals by stem and ending ("первая", "третий", "двенадцатая"), numbers whole
    static const char* const stems[] = {
        "перв", "втор", "трет", "четвёрт", "четверт", "пят", "шест", "седьм", "восьм", "девят", "десят",
        "одиннадцат", "двенадцат", "тринадцат", "четырнадцат", "пятнадцат", "шестнадцат", "семнадцат",
        "восемнадцат", "девятнадцат", "двадцат", "тридцат", "последн", "нулев"};
    static const char* const ends[] = {"ая", "ый", "ой", "ое", "ий", "ья", "ье", "ей"};
    for (const char* w : stems) {
        if (!startsWith(t, w)) continue;
        const std::string rest = t.substr(strlen(w));
        for (const char* e : ends)
            if (rest == e) return true;
    }
    static const char* const ru[] = {"один", "одна", "два", "две", "три", "четыре", "пять", "шесть", "семь",
                                     "восемь", "девять", "десять"};
    for (const char* w : ru)
        if (t == w) return true;
    return false;
}

std::string stripPunct(std::string t) {
    while (!t.empty() && strchr(".,:;!?)]-", t.back())) t.pop_back();
    while (!t.empty() && strchr("([", t.front())) t.erase(0, 1);
    return t;
}

// What a short, whole paragraph is: a part or chapter heading, or text.
// `numberOnly`: it only numbers what follows ("Глава 3", "IV"), so a short
// line after it is the chapter's name.
Kind headingKind(const std::string& text, bool* numberOnly) {
    *numberOnly = false;
    if (text.empty() || cpCount(text) > 80) return K_TEXT;
    std::vector<std::string> orig;
    for (size_t i = 0; i < text.size();) {
        while (i < text.size() && text[i] == ' ') i++;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') j++;
        if (j > i) orig.push_back(text.substr(i, j - i));
        i = j;
    }
    if (orig.empty()) return K_TEXT;
    std::vector<std::string> tok;
    for (const std::string& o : orig) tok.push_back(lowerUtf8(o));
    // A number of a heading: a roman one only in capitals ("Part I", not "Book I read").
    auto number = [&](size_t i) {
        const std::string t = stripPunct(tok[i]);
        if (!isNumberWord(t)) return false;
        if (isRoman(t)) {
            const std::string o = stripPunct(orig[i]);
            for (char c : o)
                if (c >= 'a' && c <= 'z') return false;
        }
        return true;
    };
    // ...and with words after it, one set off by punctuation or a plain number:
    // "Глава 1 Начало", "Chapter One: The Boy", not "Том два раза".
    auto setOff = [&](size_t i) {
        if (tok.size() == i + 1) return true;
        const std::string t = stripPunct(tok[i]);
        if (t.size() != tok[i].size()) return true;
        bool plain = true;
        for (char c : t) plain = plain && c >= '0' && c <= '9';
        if (plain) return true;
        const std::string& n = tok[i + 1];
        return n == "-" || startsWith(n, "\xE2\x80\x94") || startsWith(n, "\xE2\x80\x93");
    };
    const std::string first = stripPunct(tok[0]);
    // a lone roman number: "I", "IV."
    if (tok.size() == 1 && isRoman(first) && number(0)) {
        *numberOnly = true;
        return K_CHAPTER;
    }
    static const char* const parts[] = {"part", "book", "volume", "часть", "книга", "том"};
    static const char* const chapters[] = {"chapter", "section", "глава", "раздел"};
    auto numbered = [&](const char* const* words, size_t n) {
        if (tok.size() < 2) return false;
        for (size_t i = 0; i < n; i++) {
            // the word, then a number, in either order ("Часть первая", "Первая часть")
            if (first == words[i] && number(1) && setOff(1)) {
                *numberOnly = tok.size() == 2;
                return true;
            }
            if (stripPunct(tok[1]) == words[i] && number(0) && setOff(1)) {
                *numberOnly = tok.size() == 2;
                return true;
            }
        }
        return false;
    };
    if (numbered(parts, sizeof(parts) / sizeof(parts[0]))) return K_PART;
    if (numbered(chapters, sizeof(chapters) / sizeof(chapters[0]))) return K_CHAPTER;
    // Prologue, Epilogue, ... alone (or numbered)
    if (tok.size() <= 2) {
        static const char* const named[] = {"prologue", "epilogue", "preface", "foreword", "introduction",
                                            "afterword", "conclusion", "пролог", "эпилог", "предисловие",
                                            "послесловие", "введение", "вступление", "заключение"};
        for (const char* n : named)
            if (first == n && (tok.size() == 1 || number(1))) return K_CHAPTER;
    }
    return K_TEXT;
}

bool isSeparator(const std::string& t) {   // "* * *", "-----", "###"
    if (t.empty() || t.size() > 40) return false;
    bool mark = false;
    for (char c : t) {
        if (c == ' ') continue;
        if (!strchr("*-=~#_+.", c)) return false;
        mark = true;
    }
    return mark;
}

// Could follow a number-only heading as the chapter's name.
bool isHeadingName(const std::string& t) {
    if (t.empty() || cpCount(t) > 80) return false;
    const uint8_t c0 = (uint8_t)t[0];
    if (c0 == '-' || c0 == '"' || c0 == '\'' || c0 == '(' || c0 == '[') return false;
    if (startsWith(t, "\xE2\x80\x94") || startsWith(t, "\xE2\x80\x93") || startsWith(t, "\xC2\xAB") ||
        startsWith(t, "\xE2\x80\x9C") || startsWith(t, "\xE2\x80\x9E"))
        return false;   // dialogue or a quote
    const char last = t.back();
    if (strchr(",;:", last)) return false;
    // a name is not a sentence: none ending in a full stop ("Уинстону снилась
    // мать."), one ending in ! or ? only when short with no other sentence in it
    if (last == '.') return false;
    if (strchr("!?", last)) {
        if (cpCount(t) > 40) return false;
        for (size_t i = 0; i + 1 < t.size(); i++)
            if (strchr(".!?", t[i]) && t[i + 1] == ' ') return false;
    }
    return true;
}

// Statistics of a text's first bytes, fed in pieces, for detectEncoding.
struct EncodingSniffer {
    uint8_t head[3] = {0, 0, 0};
    uint32_t total = 0, zeroEven = 0, zeroOdd = 0, latin = 0;
    uint32_t high = 0, bad = 0, multi = 0;
    uint32_t score[3] = {0, 0, 0};
    uint8_t need = 0;

    void feed(const uint8_t* d, uint32_t len, bool last) {
        static const TextEncoding pages[] = {TextEncoding::Cp1251, TextEncoding::Koi8R, TextEncoding::Cp866};
        for (uint32_t i = 0; i < len; i++) {
            const uint32_t at = total + i;
            if (at < 3) head[at] = d[i];
            // UTF-16 high bytes: 0 for Latin, 4 for Cyrillic
            if (d[i] == 0 || d[i] == 4) (at & 1 ? zeroOdd : zeroEven)++;
            if ((d[i] | 0x20) >= 'a' && (d[i] | 0x20) <= 'z') latin++;
            if (d[i] >= 0x80) {
                high++;
                for (int p = 0; p < 3; p++) score[p] += isCyrLower(singleByte(pages[p], d[i]));
            }
        }
        total += len;
        (void)last;
        // UTF-8 validity, byte by byte (a sequence may be cut between pieces)
        for (uint32_t k = 0; k < len; k++) {
            const uint8_t b = d[k];
            if (need > 0) {
                if ((b & 0xC0) == 0x80) {
                    if (--need == 0) multi++;
                    continue;
                }
                bad++;
                need = 0;
            }
            if (b < 0x80) continue;
            if (b >= 0xC2 && b <= 0xDF) need = 1;
            else if (b >= 0xE0 && b <= 0xEF) need = 2;
            else if (b >= 0xF0 && b <= 0xF4) need = 3;
            else bad++;
        }
    }

    TextEncoding result(uint32_t* bomBytes) const {
        *bomBytes = 0;
        if (total >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
            *bomBytes = 3;
            return TextEncoding::Utf8;
        }
        if (total >= 2 && head[0] == 0xFF && head[1] == 0xFE) {
            *bomBytes = 2;
            return TextEncoding::Utf16LE;
        }
        if (total >= 2 && head[0] == 0xFE && head[1] == 0xFF) {
            *bomBytes = 2;
            return TextEncoding::Utf16BE;
        }
        // UTF-16 with no BOM: every other byte 0 or 4 (Latin, Cyrillic), the others rarely
        const uint32_t half = total / 2;
        if (total >= 16) {
            if (zeroOdd * 10 > half * 9 && zeroEven * 10 < half) return TextEncoding::Utf16LE;
            if (zeroEven * 10 > half * 9 && zeroOdd * 10 < half) return TextEncoding::Utf16BE;
        }
        if (high == 0 || bad * 50 <= multi) return TextEncoding::Utf8;
        // A single-byte code page: the one that reads as the most lowercase Cyrillic.
        static const TextEncoding pages[] = {TextEncoding::Cp1251, TextEncoding::Koi8R, TextEncoding::Cp866};
        int best = 0;
        for (int p = 1; p < 3; p++)
            if (score[p] > score[best]) best = p;
        // (Russian text has few Latin letters; accented Latin ones read as Cyrillic too)
        if (score[best] * 2 >= high && score[best] * 3 >= latin) return pages[best];
        return TextEncoding::Cp1252;
    }
};

// ------------------------------------------------------------ converter

enum Mode : uint8_t { M_LINE, M_BLANK, M_INDENT };

struct Piece {
    std::string text;
    bool cont = false;     // goes on from the previous piece (a long paragraph cut)
    bool whole = false;    // a complete paragraph
    uint8_t lines = 0;
    uint16_t firstLineEnd = 0;   // where its second source line starts (headings keep the break)
};

struct TocEntry {
    uint8_t depth;
    uint16_t file;
    uint16_t id;
    std::string label;
};

class TxtConverter {
public:
    TxtConverter(InFile& in, OutFile& out, const std::string& title, ProgressFn progress, void* user,
                 ConvertStats& stats)
        : _in(in), _out(out), _title(title), _progress(progress), _user(user), _stats(stats) {}
    ConvertResult run();

private:
    // input
    bool refill();
    int32_t nextCp();   // -1 at the end, -2 on a read error
    void sample();
    // paragraphs
    void feedLine(bool blank, bool indented, bool afterShort);
    void addCp(uint32_t c);
    bool endLine();
    void endParagraph();
    void cutLong(bool final);
    // structure
    void piece(Piece&& p);
    void commitPending();
    void demotePending();
    void writePiece(const Piece& p);
    void newFile();
    void ensureFile();
    void closeFile();
    void emit(const std::string& s);
    void emit(const char* s);
    bool writeNcx();
    bool writeOpf();

    InFile& _in;
    OutFile& _out;
    std::string _title;
    ProgressFn _progress;
    void* _user;
    ConvertStats& _stats;
    ZipWriter _zip;

    TextEncoding _enc = TextEncoding::Utf8;
    Mode _mode = M_LINE;
    uint32_t _wrapWidth = 0;   // where hard-wrapped lines end (90% are no longer)
    uint8_t _raw[8192];
    uint32_t _rawLen = 0, _rawPos = 0;
    uint64_t _consumed = 0;
    bool _eof = false, _readError = false;
    int _lastPermille = -1;
    std::string _lang = "en";

    // the paragraph being read
    std::string _par;
    bool _parCont = false;    // a piece of it went out already
    uint8_t _parLines = 0;
    uint16_t _parFirstLineEnd = 0;
    bool _space = false;      // whitespace waits before the next character
    bool _lineJoined = false; // the line being read went on the paragraph...
    size_t _lineJoinAt = 0;   // ...here (the space before it)

    // a heading held until the text after it shows it is one
    bool _pending = false;
    Kind _pKind = K_TEXT;
    Piece _pHead;
    bool _pNumberOnly = false;
    bool _pHasName = false;
    Piece _pName;
    std::vector<Piece> _pAfter;
    uint32_t _pAfterBytes = 0;

    // output
    uint16_t _file = 0;
    bool _fileOpen = false;
    uint32_t _fileBytes = 0;
    bool _fileHasText = false;
    bool _fileHasHeading = false;
    std::vector<std::string> _fileLabels;
    std::vector<TocEntry> _toc;
    bool _haveParts = false;
    uint16_t _nextId = 0;
};

bool TxtConverter::refill() {
    if (_rawPos < _rawLen) memmove(_raw, _raw + _rawPos, _rawLen - _rawPos);
    _rawLen -= _rawPos;
    _rawPos = 0;
    while (!_eof && _rawLen < sizeof(_raw)) {
        const int32_t n = _in.read(_raw + _rawLen, sizeof(_raw) - _rawLen);
        if (n < 0) {
            _readError = true;
            _eof = true;
            break;
        }
        if (n == 0) {
            _eof = true;
            break;
        }
        _rawLen += (uint32_t)n;
    }
    return _rawLen > 0;
}

int32_t TxtConverter::nextCp() {
    if (_rawLen - _rawPos < 4 && !_eof) refill();
    if (_rawPos >= _rawLen) return _readError ? -2 : -1;
    const uint8_t* p = _raw + _rawPos;
    const uint32_t avail = _rawLen - _rawPos;
    uint32_t c;
    switch (_enc) {
        case TextEncoding::Utf8: {
            const int n = utf8Len(p, avail, &c);
            if (n <= 0) {   // a stray byte: most likely windows-1251 text mixed in
                c = singleByte(TextEncoding::Cp1251, p[0]);
                _rawPos++;
                _consumed++;
                return (int32_t)c;
            }
            _rawPos += n;
            _consumed += n;
            return (int32_t)c;
        }
        case TextEncoding::Utf16LE:
        case TextEncoding::Utf16BE: {
            if (avail < 2) {
                _rawPos = _rawLen;
                return -1;
            }
            const bool le = _enc == TextEncoding::Utf16LE;
            c = le ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
            _rawPos += 2;
            _consumed += 2;
            if (c >= 0xD800 && c <= 0xDBFF && avail >= 4) {
                const uint32_t d = le ? (p[2] | (p[3] << 8)) : ((p[2] << 8) | p[3]);
                if (d >= 0xDC00 && d <= 0xDFFF) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                    _rawPos += 2;
                    _consumed += 2;
                }
            }
            if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
            return (int32_t)c;
        }
        default:
            _rawPos++;
            _consumed++;
            return (int32_t)singleByte(_enc, p[0]);
    }
}

// How the paragraphs are marked and what language, from the first 64 KB.
void TxtConverter::sample() {
    uint32_t lines = 0, blank = 0, indented = 0, longLines = 0, cyr = 0, lat = 0;
    uint32_t len = 0;
    uint32_t hist[152] = {0};
    bool lead = true, ind = false, any = false;
    const uint64_t start = _consumed;
    auto endLine = [&]() {
        if (!any) {
            blank++;
        } else {
            lines++;
            if (ind) indented++;
            if (len > 150) longLines++;
            hist[len > 151 ? 151 : len]++;
        }
        len = 0;
        lead = true;
        ind = false;
        any = false;
    };
    int32_t prev = 0;
    while (_consumed - start < 65536) {
        const int32_t c = nextCp();
        if (c < 0) break;
        if (c == '\n' && prev == '\r') {
            prev = c;
            continue;
        }
        prev = c;
        if (c == '\n' || c == '\r') {
            endLine();
            continue;
        }
        const bool ws = c == ' ' || c == '\t' || c == 0xA0 || c == 0x3000 || c == 0xFEFF;
        if (lead && ws) {
            if (c != 0xFEFF) ind = true;
            continue;
        }
        lead = false;
        if (!ws) any = true;
        len++;
        if ((c >= 0x410 && c <= 0x44F) || c == 0x401 || c == 0x451) cyr++;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) lat++;
    }
    if (len) endLine();
    _lang = cyr > lat ? "ru" : "en";
    for (uint32_t w = 151, above = 0; w > 0; w--) {
        above += hist[w];
        if (above * 10 >= lines) {
            _wrapWidth = w;
            break;
        }
    }
    if (lines == 0 || longLines * 4 > lines) _mode = M_LINE;
    else if (indented * 12 > lines && indented * 10 < lines * 8) _mode = M_INDENT;
    else if (blank * 6 > lines) _mode = M_BLANK;
    else _mode = M_LINE;
}

void TxtConverter::emit(const std::string& s) {
    _zip.write(s.data(), (uint32_t)s.size());
    _fileBytes += (uint32_t)s.size();
}
void TxtConverter::emit(const char* s) {
    _zip.write(s, (uint32_t)strlen(s));
    _fileBytes += (uint32_t)strlen(s);
}

void TxtConverter::ensureFile() {
    if (_fileOpen) return;
    _file++;
    char name[32];
    snprintf(name, sizeof(name), "OEBPS/c%04u.xhtml", (unsigned)_file);
    _zip.open(name);
    _fileOpen = true;
    _fileBytes = 0;
    _fileHasText = false;
    _fileHasHeading = false;
    emit("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"" + _lang + "\"><head><title>" + xmlEscape(_title) +
         "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>\n");
    _fileLabels.resize(_file);
}

void TxtConverter::closeFile() {
    if (!_fileOpen) return;
    emit("</body></html>\n");
    _zip.close();
    _fileOpen = false;
}

void TxtConverter::newFile() {
    if (_fileOpen && (_fileHasText || _fileHasHeading)) closeFile();
}

void TxtConverter::writePiece(const Piece& p) {
    // A file is kept to ~40 KB, so that one never takes long to lay out:
    // split between paragraphs, or inside a very long one.
    if (_fileOpen && _fileHasText && ((!p.cont && _fileBytes >= 40000) || _fileBytes >= 64000)) closeFile();
    ensureFile();
    if (p.whole && isSeparator(p.text)) {
        emit("<p class=\"sep\">" + xmlEscape(p.text) + "</p>\n");
        return;
    }
    emit(p.cont ? "<p class=\"cont\">" : "<p>");
    emit(xmlEscape(p.text));
    emit("</p>\n");
    _fileHasText = true;
    std::string& l = _fileLabels[_file - 1];
    if (l.size() < 96) {
        if (!l.empty()) l += ' ';
        l += p.text.substr(0, 120);
    }
}

void TxtConverter::commitPending() {
    if (!_pending) return;
    _pending = false;
    // a chapter right after its part's heading stays in the part's file
    if (!(_pKind == K_CHAPTER && _fileOpen && !_fileHasText && _fileHasHeading)) newFile();
    ensureFile();
    const uint16_t id = ++_nextId;
    const char* tag = _pKind == K_PART ? "h1" : "h2";
    std::string html = xmlEscape(_pHead.text), label = _pHead.text;
    if (_pHead.lines >= 2 && _pHead.firstLineEnd > 0 && _pHead.firstLineEnd < _pHead.text.size()) {
        // "CHAPTER I.\nDown the Rabbit-Hole": keep the line break
        html = xmlEscape(_pHead.text.substr(0, _pHead.firstLineEnd)) + "<br/>" +
               xmlEscape(_pHead.text.substr(_pHead.firstLineEnd + 1));
    }
    if (_pHasName) {
        html += "<br/>" + xmlEscape(_pName.text);
        label += (label.empty() || strchr(".:", label.back()) ? " " : ". ") + _pName.text;
    }
    char open[48];
    snprintf(open, sizeof(open), "<%s id=\"h%u\">", tag, (unsigned)id);
    emit(std::string(open) + html + "</" + tag + ">\n");
    _fileHasHeading = true;
    if (_pKind == K_PART) _haveParts = true;
    _toc.push_back(TocEntry{(uint8_t)(_pKind == K_PART ? 0 : 1), _file, id, label});
    _pHasName = false;
    std::vector<Piece> after;
    after.swap(_pAfter);
    _pAfterBytes = 0;
    for (const Piece& p : after) writePiece(p);
}

void TxtConverter::demotePending() {
    if (!_pending) return;
    _pending = false;
    _pHead.whole = false;   // not a separator either
    writePiece(_pHead);
    if (_pHasName) writePiece(_pName);
    _pHasName = false;
    std::vector<Piece> after;
    after.swap(_pAfter);
    _pAfterBytes = 0;
    for (const Piece& p : after) writePiece(p);
}

void TxtConverter::piece(Piece&& p) {
    bool numberOnly = false;
    const Kind k = p.whole ? headingKind(p.text, &numberOnly) : K_TEXT;
    if (k != K_TEXT) {
        if (_pending) {
            // "Часть первая" then "Глава 1": both; two headings of a kind with no
            // text between are a contents list: text.
            if (_pKind == K_PART && k == K_CHAPTER && _pAfterBytes == 0) commitPending();
            else demotePending();
        }
        _pending = true;
        _pKind = k;
        _pHead = std::move(p);
        _pNumberOnly = numberOnly;
        _pHasName = false;
        return;
    }
    if (!_pending) {
        writePiece(p);
        return;
    }
    if (_pNumberOnly && !_pHasName && _pAfter.empty() && p.whole && isHeadingName(p.text)) {
        _pName = std::move(p);
        _pHasName = true;
        return;
    }
    _pAfterBytes += (uint32_t)p.text.size();
    _pAfter.push_back(std::move(p));
    if (_pAfterBytes >= 200 || _pAfter.size() >= 4) commitPending();
}

// A paragraph is cut where the engine would otherwise cut it (~3 KB): at the
// end of a sentence if there is one in the last ~800 bytes, else at a space.
void TxtConverter::cutLong(bool final) {
    constexpr size_t kLimit = 2400;
    while (_par.size() > kLimit + (final ? 0 : 600)) {
        size_t cut = kLimit;
        size_t sentence = cut;
        while (sentence > 1 && cut - sentence < 800 &&
               !(_par[sentence - 1] == ' ' && strchr(".!?", _par[sentence - 2])))
            sentence--;
        if (sentence > 1 && cut - sentence < 800) {
            cut = sentence;
        } else {
            while (cut > 0 && _par[cut - 1] != ' ') cut--;
            if (cut == 0) {
                cut = kLimit;
                while (cut > 0 && ((uint8_t)_par[cut] & 0xC0) == 0x80) cut--;
            }
        }
        Piece p;
        p.text = _par.substr(0, cut);
        while (!p.text.empty() && p.text.back() == ' ') p.text.pop_back();
        p.cont = _parCont;
        p.lines = _parLines;
        _parCont = true;
        _par.erase(0, cut);
        while (!_par.empty() && _par[0] == ' ') _par.erase(0, 1);
        _parFirstLineEnd = 0;
        _lineJoined = false;
        if (!p.text.empty()) piece(std::move(p));
    }
}

// A line ended. One that went on a paragraph but is a heading ("II" right
// after a paragraph's last line, with no indent) is taken back out: true then,
// and the next line starts a paragraph too.
bool TxtConverter::endLine() {
    const bool joined = _lineJoined;
    _lineJoined = false;
    if (!joined || _mode == M_LINE || _lineJoinAt >= _par.size() || _par.size() - _lineJoinAt > 300) return false;
    std::string line = _par.substr(_lineJoinAt + 1);
    bool numberOnly;
    if (headingKind(line, &numberOnly) == K_TEXT) return false;
    _par.resize(_lineJoinAt);
    if (_parLines > 0) _parLines--;
    endParagraph();
    _par = std::move(line);
    _parLines = 1;
    endParagraph();
    return true;
}

void TxtConverter::endParagraph() {
    cutLong(true);
    if (!_par.empty()) {
        Piece p;
        p.text = std::move(_par);
        p.cont = _parCont;
        p.whole = !_parCont;
        p.lines = _parLines;
        p.firstLineEnd = _parFirstLineEnd;
        piece(std::move(p));
    }
    _par.clear();
    _parCont = false;
    _parLines = 0;
    _parFirstLineEnd = 0;
    _space = false;
}

void TxtConverter::addCp(uint32_t c) {
    if (c == '\t' || c == ' ' || c == 0x3000 || c == 0x2028 || c == 0x2029) {
        _space = !_par.empty();
        return;
    }
    if (c < 0x20 || c == 0x7F || c == 0xFEFF || (c >= 0x80 && c < 0xA0) || c == 0xFFFE || c == 0xFFFF) return;
    if (_space) {
        _par += ' ';
        _space = false;
    }
    putUtf8(_par, c);
}

// A line starts (or is blank): where paragraphs begin and end.
void TxtConverter::feedLine(bool blank, bool indented, bool afterShort) {
    if (blank) {
        endParagraph();
        return;
    }
    // With indents, a line after one that ended well short of the wrap also
    // starts a paragraph: a heading, or one with no indent of its own.
    const bool starts = _mode == M_LINE || (_mode == M_INDENT && (indented || afterShort));
    if (starts) {
        endParagraph();
    } else if (!_par.empty() || _parCont) {
        if (_parLines == 1 && _parFirstLineEnd == 0 && _par.size() < 0xFFFF) _parFirstLineEnd = (uint16_t)_par.size();
        _space = true;
        _lineJoined = !_par.empty();
        _lineJoinAt = _par.size();
    }
    if (_parLines < 255) _parLines++;
}

ConvertResult TxtConverter::run() {
    if (!_in.rewind() || !refill()) {
        _stats.error = _readError ? "read error" : "empty file";
        if (_readError) return ConvertResult::IoError;
    }
    // The encoding, from the first 64 KB...
    EncodingSniffer sniff;
    for (;;) {
        const bool last = _eof || _consumed + _rawLen >= 65536;
        sniff.feed(_raw, _rawLen, last);
        _consumed += _rawLen;
        _rawPos = _rawLen;
        if (last || !refill()) break;
    }
    uint32_t bom = 0;
    _enc = sniff.result(&bom);
    // ...and how its paragraphs are marked, from as much again
    if (!_in.rewind()) return ConvertResult::IoError;
    _rawLen = _rawPos = 0;
    _eof = false;
    _consumed = 0;
    refill();
    _rawPos = bom;
    _consumed = bom;
    sample();
    if (!_in.rewind()) return ConvertResult::IoError;
    _rawLen = _rawPos = 0;
    _eof = false;
    _consumed = 0;
    refill();
    _rawPos = bom;
    _consumed = bom;

    _zip.begin(&_out);
    _zip.add("mimetype", "application/epub+zip");
    _zip.add("META-INF/container.xml",
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
             "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
             "</rootfiles></container>\n");
    _zip.add("OEBPS/style.css", kCss);

    const uint64_t total = _in.size() ? _in.size() : 1;
    bool lineStart = true, indented = false, prevShort = false;
    uint32_t lineLen = 0;
    int32_t prev = 0;
    for (;;) {
        const int32_t c = nextCp();
        if (c == -2) {
            _stats.error = "read error";
            return ConvertResult::IoError;
        }
        if (c < 0) break;
        if (c == '\n' && prev == '\r') {
            prev = c;
            continue;
        }
        prev = c;
        if (c == '\n' || c == '\r' || c == 0x0C) {
            const bool split = !lineStart && endLine();
            if (lineStart) feedLine(true, false, false);
            lineStart = true;
            prevShort = split || lineLen * 10 < _wrapWidth * 6;
            lineLen = 0;
            indented = false;
            if (_progress) {
                const int pm = (int)(_consumed * 990 / total);
                if (pm / 10 != _lastPermille / 10) {
                    _lastPermille = pm;
                    _progress(_user, pm);
                }
            }
            continue;
        }
        if (lineStart) {
            if (c == ' ' || c == '\t' || c == 0xA0 || c == 0x3000 || c == 0xFEFF) {
                if (c != 0xFEFF) indented = true;
                continue;
            }
            lineStart = false;
            feedLine(false, indented, prevShort);
        }
        lineLen++;
        addCp((uint32_t)c);
        if (_par.size() > 3000) cutLong(false);
        if (_zip.failed()) break;
    }
    endParagraph();
    if (_pending) {
        if (_pAfterBytes > 0 || _pHasName) commitPending();
        else demotePending();
    }
    if (_file == 0) {
        ensureFile();
        emit("<p>&#160;</p>\n");
    }
    closeFile();
    if (_zip.failed() || !writeNcx() || !writeOpf() || !_zip.finish()) {
        _stats.error = "writing the EPUB failed";
        return ConvertResult::IoError;
    }
    _stats.chapters = _file;
    _stats.title = _title;
    _stats.lang = _lang;
    _stats.error = encodingName(_enc);   // not an error: for the log
    _stats.error += _mode == M_LINE ? ", a paragraph per line" : _mode == M_BLANK ? ", blank-line paragraphs"
                                                                                   : ", indented paragraphs";
    if (_progress) _progress(_user, 1000);
    return ConvertResult::Ok;
}

bool TxtConverter::writeNcx() {
    if (!_zip.open("OEBPS/toc.ncx")) return false;
    auto put = [&](const std::string& s) { _zip.write(s.data(), (uint32_t)s.size()); };
    put("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head>"
        "<meta name=\"dtb:uid\" content=\"txt-" + std::to_string(_in.size()) + "\"/></head><docTitle><text>" +
        xmlEscape(_title) + "</text></docTitle><navMap>\n");
    uint32_t order = 0;
    int open = 0;
    auto nav = [&](int depth, const std::string& label, uint16_t file, int id) {
        while (open > depth) {
            put("</navPoint>\n");
            open--;
        }
        char src[40];
        if (id > 0) snprintf(src, sizeof(src), "c%04u.xhtml#h%d", (unsigned)file, id);
        else snprintf(src, sizeof(src), "c%04u.xhtml", (unsigned)file);
        order++;
        put("<navPoint id=\"n" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) +
            "\"><navLabel><text>" + xmlEscape(label) + "</text></navLabel><content src=\"" + src + "\"/>");
        open++;
    };
    auto shortLabel = [](std::string c) {   // ~48 characters, cut at a space
        if (cpCount(c) <= 48) return c;
        size_t at = 0;
        for (size_t n = 0; at < c.size() && n < 48; at++)
            if (((uint8_t)c[at] & 0xC0) != 0x80) n++;
        while (at < c.size() && ((uint8_t)c[at] & 0xC0) == 0x80) at++;
        size_t cut = c.rfind(' ', at);
        if (cut == std::string::npos || cut < at / 2) cut = at;
        return c.substr(0, cut) + "\xE2\x80\xA6";
    };
    if (!_toc.empty()) {
        // the text before the first heading (a dedication, a preface) gets an entry too
        if (_toc[0].file > 1) nav(0, shortLabel(_fileLabels.empty() ? _title : _fileLabels[0]), 1, 0);
        for (const TocEntry& t : _toc) {
            // chapters nest under parts; with no parts they are the top level
            const int depth = _haveParts ? t.depth : 0;
            nav(std::min(depth, open), t.label, t.file, t.id);
        }
    } else if (_file > 1) {
        // No headings found: one entry per file, by its first words.
        for (uint16_t f = 1; f <= _file; f++) {
            const std::string l = f <= _fileLabels.size() ? shortLabel(_fileLabels[f - 1]) : std::string();
            nav(0, std::to_string(f) + (l.empty() ? "" : ". " + l), f, 0);
        }
    }
    if (order == 0) nav(0, _title.empty() ? std::string("1") : _title, 1, 0);
    while (open > 0) {
        put("</navPoint>\n");
        open--;
    }
    _stats.tocEntries = order;
    put("</navMap></ncx>\n");
    return _zip.close();
}

bool TxtConverter::writeOpf() {
    if (!_zip.open("OEBPS/content.opf")) return false;
    auto put = [&](const std::string& s) { _zip.write(s.data(), (uint32_t)s.size()); };
    put("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">\n");
    put("<dc:identifier id=\"uid\">txt-" + std::to_string(_in.size()) + "</dc:identifier>\n");
    put("<dc:title>" + xmlEscape(_title.empty() ? std::string("?") : _title) + "</dc:title>\n");
    put("<dc:language>" + _lang + "</dc:language>\n");
    put("</metadata>\n<manifest>\n"
        "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
        "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n");
    char buf[128];
    for (uint16_t f = 1; f <= _file; f++) {
        snprintf(buf, sizeof(buf), "<item id=\"c%04u\" href=\"c%04u.xhtml\" media-type=\"application/xhtml+xml\"/>\n",
                 (unsigned)f, (unsigned)f);
        put(buf);
    }
    put("</manifest>\n<spine toc=\"ncx\">\n");
    for (uint16_t f = 1; f <= _file; f++) {
        snprintf(buf, sizeof(buf), "<itemref idref=\"c%04u\"/>\n", (unsigned)f);
        put(buf);
    }
    put("</spine>\n</package>\n");
    return _zip.close();
}

}  // namespace

TextEncoding detectEncoding(const uint8_t* d, uint32_t len, uint32_t* bomBytes) {
    EncodingSniffer s;
    s.feed(d, len, true);
    return s.result(bomBytes);
}

ConvertResult txtToEpub(InFile& in, OutFile& out, const std::string& title, ProgressFn progress, void* user,
                        ConvertStats* stats) {
    ConvertStats local;
    ConvertStats& s = stats ? *stats : local;
    TxtConverter* c = new (std::nothrow) TxtConverter(in, out, title, progress, user, s);
    if (!c) return ConvertResult::NoMemory;
    const ConvertResult r = c->run();
    delete c;
    return r;
}

}  // namespace convert
