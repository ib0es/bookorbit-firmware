#include "koreader_sync_mapper.h"

int KOReaderSyncMapper::parseIndex(const String& xpath, const char* prefix, bool last) {
    if (xpath.length() == 0 || !prefix) return -1;
    size_t prefixLen = strlen(prefix);
    
    int pos = -1;
    if (last) {
        pos = xpath.lastIndexOf(prefix);
    } else {
        pos = xpath.indexOf(prefix);
    }
    if (pos < 0) return -1;

    size_t numStart = pos + prefixLen;
    int bracketEnd = xpath.indexOf(']', numStart);
    if (bracketEnd < 0) return -1;

    int val = 0;
    for (int i = numStart; i < bracketEnd; i++) {
        char c = xpath[i];
        if (c < '0' || c > '9') return -1;
        val = val * 10 + (c - '0');
    }
    return (val > 0) ? val : 1;
}

size_t KOReaderSyncMapper::parseCharOffset(const String& xpath) {
    int dotPos = xpath.lastIndexOf('.');
    if (dotPos < 0 || dotPos + 1 >= (int)xpath.length()) return 0;

    size_t val = 0;
    for (int i = dotPos + 1; i < (int)xpath.length(); i++) {
        char c = xpath[i];
        if (c < '0' || c > '9') break;
        val = val * 10 + (c - '0');
    }
    return val;
}

ParsedXPath KOReaderSyncMapper::parseXPath(const String& xpath) {
    ParsedXPath res;
    if (xpath.length() == 0) return res;

    // 1. Check for /body/DocFragment[N]
    int docFrag = parseIndex(xpath, "/body/DocFragment[");
    if (docFrag >= 1) {
        res.spineIndex = docFrag - 1; // 0-based
        res.isValid = true;
    } else {
        // Fallback: check if entire string is just a numeric page/fragment
        if (xpath.startsWith("/")) {
            // Some other XML root without DocFragment
            res.spineIndex = 0;
            res.isValid = true;
        } else {
            return res;
        }
    }

    // 2. Extract paragraph index if present: /p[N], /li[N], /div[N]
    int pIdx = parseIndex(xpath, "/p[", true);
    if (pIdx < 0) pIdx = parseIndex(xpath, "/li[", true);
    if (pIdx < 0) pIdx = parseIndex(xpath, "/div[", true);

    if (pIdx >= 1) {
        res.paragraphIndex = pIdx;
    } else {
        res.paragraphIndex = 1;
    }

    // 3. Extract character offset
    res.charOffset = parseCharOffset(xpath);

    return res;
}

String KOReaderSyncMapper::buildXPath(int spineIndex, int paragraphIndex, size_t charOffset) {
    int docFrag = (spineIndex >= 0) ? (spineIndex + 1) : 1;
    String xpath = "/body/DocFragment[" + String(docFrag) + "]/body";
    
    if (paragraphIndex > 1 || charOffset > 0) {
        xpath += "/p[" + String(paragraphIndex > 0 ? paragraphIndex : 1) + "]";
        xpath += "/text()." + String((unsigned long)charOffset);
    }
    return xpath;
}
