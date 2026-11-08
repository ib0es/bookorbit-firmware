#pragma once

#include <Arduino.h>
#include "../hal/storage.h"

// Streaming HTML/XML stripper with 4KB write buffering and chapter heading capture
class StreamingHtmlStripper {
public:
    FsFile* outFile;
    bool inTag;
    bool inComment;
    bool inEntity;
    bool inHead;
    bool inStyle;
    bool inScript;

    char tagBuf[256];
    size_t tagLen;

    char entityBuf[32];
    size_t entityLen;

    int consecutiveNewlines;
    char lastChar;

    bool capturingHeading;
    char headingBuf[256];
    size_t headingLen;
    String firstHeading;

    uint8_t* writeBuf;
    size_t writeBufSize;
    size_t writeBufPos;

    // countOnly: no output file, only count the bytes the text would have
    // (used to map an EPUB CFI to an offset in the converted text).
    StreamingHtmlStripper(FsFile* out, bool countOnly = false);

    bool countOnly;
    uint32_t emitted;   // bytes of text produced so far (written or counted)
    bool active() const { return outFile != nullptr || countOnly; }
    ~StreamingHtmlStripper();

    void flush();
    void writeByte(uint8_t b);
    void writeBytes(const uint8_t* p, size_t n);
    void writeUnicode(uint32_t cp);
    void resetForChapter();
    String getFirstHeading() const;
    void processChunk(const uint8_t* data, size_t len);
    void emitNewline(int count = 1);
    void writeChar(char c);
    void writeRaw(const uint8_t* p, size_t n);
    void handleTag(const char* tag);
    void handleEntity(const char* ent);
};
