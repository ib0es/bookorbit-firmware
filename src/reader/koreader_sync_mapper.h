#pragma once

#include <Arduino.h>

struct ParsedXPath {
    int spineIndex = -1;       // 0-based spine item index (-1 if unknown)
    int paragraphIndex = 1;    // 1-based paragraph index
    size_t charOffset = 0;     // character offset within text node
    bool isValid = false;
};

class KOReaderSyncMapper {
public:
    // Parses a KOReader XPath string such as:
    // "/body/DocFragment[3]/body/div/p[5]/text().12"
    // "/body/DocFragment[3]/body/p[5]"
    // "/body/DocFragment[3]/body"
    // Returns ParsedXPath with 0-based spineIndex and 1-based paragraphIndex
    static ParsedXPath parseXPath(const String& xpath);

    // Generates a canonical KOReader XPath string
    static String buildXPath(int spineIndex, int paragraphIndex = 1, size_t charOffset = 0);

private:
    static int parseIndex(const String& xpath, const char* prefix, bool last = false);
    static size_t parseCharOffset(const String& xpath);
};
