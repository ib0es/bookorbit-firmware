#include "html_stripper.h"

StreamingHtmlStripper::StreamingHtmlStripper(FsFile* out, bool countOnly_)
    : outFile(out), inTag(false), inComment(false), inEntity(false),
      inHead(false), inStyle(false), inScript(false),
      tagLen(0), entityLen(0), consecutiveNewlines(2), lastChar('\n'),
      capturingHeading(false), headingLen(0), firstHeading(""),
      writeBuf(nullptr), writeBufSize(4096), writeBufPos(0), countOnly(countOnly_), emitted(0) {
    if (outFile) writeBuf = (uint8_t*)malloc(writeBufSize);
}

StreamingHtmlStripper::~StreamingHtmlStripper() {
    flush();
    if (writeBuf) {
        free(writeBuf);
        writeBuf = nullptr;
    }
}

void StreamingHtmlStripper::flush() {
    if (outFile && writeBuf && writeBufPos > 0) {
        outFile->write(writeBuf, writeBufPos);
        writeBufPos = 0;
    }
}

void StreamingHtmlStripper::writeByte(uint8_t b) {
    if (!active()) return;
    emitted++;
    if (!outFile) return;
    if (!writeBuf) {
        outFile->write(b);
        return;
    }
    writeBuf[writeBufPos++] = b;
    if (writeBufPos >= writeBufSize) {
        outFile->write(writeBuf, writeBufPos);
        writeBufPos = 0;
    }
}

void StreamingHtmlStripper::writeBytes(const uint8_t* p, size_t n) {
    if (!active() || n == 0) return;
    emitted += n;
    if (!outFile) return;
    if (!writeBuf) {
        outFile->write(p, n);
        return;
    }
    while (n > 0) {
        size_t space = writeBufSize - writeBufPos;
        if (n < space) {
            memcpy(writeBuf + writeBufPos, p, n);
            writeBufPos += n;
            return;
        }
        memcpy(writeBuf + writeBufPos, p, space);
        writeBufPos += space;
        outFile->write(writeBuf, writeBufSize);
        writeBufPos = 0;
        p += space;
        n -= space;
    }
}

void StreamingHtmlStripper::writeUnicode(uint32_t cp) {
    if (!active() || cp == 0) return;
    if (cp == 160) { // &nbsp; -> space
        writeChar(' ');
        return;
    }
    if (cp < 0x80) {
        writeByte((uint8_t)cp);
    } else if (cp < 0x800) {
        uint8_t buf[2];
        buf[0] = (uint8_t)(0xC0 | (cp >> 6));
        buf[1] = (uint8_t)(0x80 | (cp & 0x3F));
        writeBytes(buf, 2);
    } else if (cp < 0x10000) {
        uint8_t buf[3];
        buf[0] = (uint8_t)(0xE0 | (cp >> 12));
        buf[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (cp & 0x3F));
        writeBytes(buf, 3);
    } else if (cp <= 0x10FFFF) {
        uint8_t buf[4];
        buf[0] = (uint8_t)(0xF0 | (cp >> 18));
        buf[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (uint8_t)(0x80 | (cp & 0x3F));
        writeBytes(buf, 4);
    }
}

void StreamingHtmlStripper::resetForChapter() {
    firstHeading = "";
    capturingHeading = false;
    headingLen = 0;
    inHead = false;
    inStyle = false;
    inScript = false;
    inTag = false;
    inComment = false;
    inEntity = false;
    tagLen = 0;
    entityLen = 0;
}

String StreamingHtmlStripper::getFirstHeading() const {
    return firstHeading;
}

void StreamingHtmlStripper::processChunk(const uint8_t* data, size_t len) {
    if (!active() || len == 0) return;
    // Let the IDLE task run (task watchdog) now and then. Not on every call: the
    // CFI walker feeds one byte per call, and a 1 ms tick per byte made finding a
    // reading position take ~1 s per 1000 characters of the chapter (12 s on Picco).
    static uint32_t s_lastYield = 0;
    const uint32_t now = millis();
    if (now - s_lastYield >= 50) {
        s_lastYield = now;
        yield();
        vTaskDelay(1);
    }

    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];

        if (inComment) {
            // Inside <!-- ... -->
            if (tagLen < sizeof(tagBuf) - 1) {
                tagBuf[tagLen++] = c;
            }
            if (c == '>' && tagLen >= 3 && tagBuf[tagLen - 2] == '-' && tagBuf[tagLen - 3] == '-') {
                inComment = false;
                inTag = false;
                tagLen = 0;
            }
            continue;
        }

        if (inTag) {
            if (tagLen < sizeof(tagBuf) - 1) {
                tagBuf[tagLen++] = c;
            }
            // Check if comment starts
            if (tagLen == 3 && strncmp(tagBuf, "!--", 3) == 0) {
                inComment = true;
                continue;
            }
            if (c == '>') {
                // Tag complete
                tagBuf[tagLen < sizeof(tagBuf) ? tagLen : sizeof(tagBuf) - 1] = '\0';
                handleTag(tagBuf);
                inTag = false;
                tagLen = 0;
            }
            continue;
        }

        if (inEntity) {
            if (c == ';') {
                if (entityLen < sizeof(entityBuf) - 1) {
                    entityBuf[entityLen++] = ';';
                }
                entityBuf[entityLen < sizeof(entityBuf) ? entityLen : sizeof(entityBuf) - 1] = '\0';
                handleEntity(entityBuf);
                inEntity = false;
                entityLen = 0;
                continue;
            }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '<' || entityLen >= sizeof(entityBuf) - 2) {
                // Malformed entity, write raw buffered chars
                entityBuf[entityLen < sizeof(entityBuf) ? entityLen : sizeof(entityBuf) - 1] = '\0';
                writeRaw((const uint8_t*)entityBuf, entityLen);
                inEntity = false;
                entityLen = 0;
                if (c == '<') {
                    inTag = true;
                    tagLen = 0;
                } else {
                    writeChar(c);
                }
                continue;
            }
            entityBuf[entityLen++] = c;
            continue;
        }

        // Normal text
        if (c == '<') {
            inTag = true;
            tagLen = 0;
        } else if (c == '&') {
            inEntity = true;
            entityLen = 0;
            entityBuf[entityLen++] = '&';
        } else {
            writeChar(c);
        }
    }
}

void StreamingHtmlStripper::emitNewline(int count) {
    if (inHead || inStyle || inScript) return;
    while (count-- > 0 && consecutiveNewlines < 2) {
        writeByte('\n');
        consecutiveNewlines++;
        lastChar = '\n';
    }
}

void StreamingHtmlStripper::writeChar(char c) {
    if (!active() || inHead || inStyle || inScript) return;
    if (c == '\r') return;

    // Capture heading characters
    if (capturingHeading && headingLen < sizeof(headingBuf) - 1) {
        if (c == '\n' || c == '\t') {
            if (headingLen > 0 && headingBuf[headingLen - 1] != ' ') {
                headingBuf[headingLen++] = ' ';
            }
        } else {
            headingBuf[headingLen++] = c;
        }
    }

    if (c == '\n') {
        if (consecutiveNewlines < 2) {
            writeByte('\n');
            consecutiveNewlines++;
            lastChar = '\n';
        }
        return;
    }

    if (c == ' ' || c == '\t') {
        if (lastChar != ' ' && lastChar != '\n') {
            writeByte(' ');
            lastChar = ' ';
        }
        return;
    }

    // Output character (including multi-byte UTF-8 bytes)
    writeByte((uint8_t)c);
    consecutiveNewlines = 0;
    lastChar = c;
}

void StreamingHtmlStripper::writeRaw(const uint8_t* p, size_t n) {
    if (!active() || inHead || inStyle || inScript || n == 0) return;
    writeBytes(p, n);
    consecutiveNewlines = 0;
    lastChar = (char)p[n - 1];

    if (capturingHeading) {
        for (size_t i = 0; i < n && headingLen < sizeof(headingBuf) - 1; i++) {
            headingBuf[headingLen++] = (char)p[i];
        }
    }
}

void StreamingHtmlStripper::handleTag(const char* tag) {
    const char* p = tag;
    while (*p == ' ' || *p == '\t') p++;

    char name[32];
    size_t idx = 0;
    if (*p == '/') {
        name[idx++] = '/';
        p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    while (*p && *p != ' ' && *p != '\t' && *p != '/' && *p != '>' && idx < sizeof(name) - 1) {
        name[idx++] = tolower((unsigned char)*p++);
    }
    name[idx] = '\0';

    if (strcmp(name, "head") == 0) inHead = true;
    else if (strcmp(name, "/head") == 0 || strcmp(name, "body") == 0) inHead = false;
    else if (strcmp(name, "style") == 0) inStyle = true;
    else if (strcmp(name, "/style") == 0) inStyle = false;
    else if (strcmp(name, "script") == 0) inScript = true;
    else if (strcmp(name, "/script") == 0) inScript = false;

    // Heading capture check
    if (strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 || strcmp(name, "h3") == 0 || strcmp(name, "title") == 0) {
        if (firstHeading.length() == 0 && !capturingHeading) {
            capturingHeading = true;
            headingLen = 0;
        }
    } else if (strcmp(name, "/h1") == 0 || strcmp(name, "/h2") == 0 || strcmp(name, "/h3") == 0 || strcmp(name, "/title") == 0) {
        if (capturingHeading) {
            headingBuf[headingLen < sizeof(headingBuf) ? headingLen : sizeof(headingBuf) - 1] = '\0';
            firstHeading = String(headingBuf);
            firstHeading.trim();
            capturingHeading = false;
        }
    }

    if (inHead || inStyle || inScript) return;

    // Headings (translate to Markdown headers for styled rendering)
    if (strcmp(name, "h1") == 0) {
        emitNewline(2);
        writeRaw((const uint8_t*)"# ", 2);
    } else if (strcmp(name, "/h1") == 0) {
        emitNewline(2);
    } else if (strcmp(name, "h2") == 0) {
        emitNewline(2);
        writeRaw((const uint8_t*)"## ", 3);
    } else if (strcmp(name, "/h2") == 0) {
        emitNewline(2);
    } else if (strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0) {
        emitNewline(2);
        writeRaw((const uint8_t*)"### ", 4);
    } else if (strcmp(name, "/h3") == 0 || strcmp(name, "/h4") == 0) {
        emitNewline(2);
    } else if (strcmp(name, "p") == 0 || strcmp(name, "/p") == 0 ||
               strcmp(name, "blockquote") == 0 || strcmp(name, "/blockquote") == 0) {
        emitNewline(2);
    } else if (strcmp(name, "div") == 0 || strcmp(name, "/div") == 0 ||
               strcmp(name, "br") == 0 || strcmp(name, "br/") == 0 ||
               strcmp(name, "tr") == 0 || strcmp(name, "/tr") == 0) {
        emitNewline(1);
    } else if (strcmp(name, "li") == 0) {
        emitNewline(1);
        writeRaw((const uint8_t*)"  * ", 4);
    } else if (strcmp(name, "hr") == 0 || strcmp(name, "hr/") == 0) {
        emitNewline(2);
        writeRaw((const uint8_t*)"* * *\n\n", 7);
        consecutiveNewlines = 2;
        lastChar = '\n';
    } else if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0 ||
               strcmp(name, "/b") == 0 || strcmp(name, "/strong") == 0) {
        writeRaw((const uint8_t*)"**", 2);
    }
}

void StreamingHtmlStripper::handleEntity(const char* ent) {
    if (inHead || inStyle || inScript) return;

    if (strcmp(ent, "&nbsp;") == 0) {
        writeChar(' ');
    } else if (strcmp(ent, "&quot;") == 0) {
        writeChar('"');
    } else if (strcmp(ent, "&apos;") == 0) {
        writeChar('\'');
    } else if (strcmp(ent, "&amp;") == 0) {
        writeChar('&');
    } else if (strcmp(ent, "&lt;") == 0) {
        writeChar('<');
    } else if (strcmp(ent, "&gt;") == 0) {
        writeChar('>');
    } else if (strcmp(ent, "&mdash;") == 0) {
        writeRaw((const uint8_t*)"\xE2\x80\x94", 3);
    } else if (strcmp(ent, "&ndash;") == 0) {
        writeRaw((const uint8_t*)"\xE2\x80\x93", 3);
    } else if (strcmp(ent, "&laquo;") == 0) {
        writeRaw((const uint8_t*)"\xC2\xAB", 2);
    } else if (strcmp(ent, "&raquo;") == 0) {
        writeRaw((const uint8_t*)"\xC2\xBB", 2);
    } else if (strcmp(ent, "&hellip;") == 0) {
        writeRaw((const uint8_t*)"\xE2\x80\xA6", 3);
    } else if (strncmp(ent, "&#x", 3) == 0 || strncmp(ent, "&#X", 3) == 0) {
        uint32_t cp = strtoul(ent + 3, NULL, 16);
        writeUnicode(cp);
        consecutiveNewlines = 0;
        lastChar = 'x';
        if (capturingHeading && headingLen < sizeof(headingBuf) - 1) {
            headingBuf[headingLen++] = ' ';
        }
    } else if (strncmp(ent, "&#", 2) == 0) {
        uint32_t cp = strtoul(ent + 2, NULL, 10);
        writeUnicode(cp);
        consecutiveNewlines = 0;
        lastChar = 'x';
        if (capturingHeading && headingLen < sizeof(headingBuf) - 1) {
            headingBuf[headingLen++] = ' ';
        }
    } else {
        writeRaw((const uint8_t*)ent, strlen(ent));
    }
}
