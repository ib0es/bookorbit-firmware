#pragma once

#include <Arduino.h>

class KOReaderDocumentId {
public:
    static const size_t CHUNK_SIZE = 1024;
    static const int OFFSET_COUNT = 12;

    // Calculate KOReader document ID (partial MD5 hash of exponential offsets).
    // Matches KOReader's document hash algorithm 1:1.
    static String calculate(const String& filePath);

    // Calculate MD5 hash of filename (fallback when content hashing is not possible).
    static String calculateFromFilename(const String& filePath);

    // Get byte offset for chunk i (i = -1 .. 10)
    static size_t getOffset(int i);
};
