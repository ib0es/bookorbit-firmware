#include "koreader_doc_id.h"
#include <MD5Builder.h>
#include "../hal/storage.h"

static String getFilename(const String& path) {
    int idx = path.lastIndexOf('/');
    if (idx >= 0) {
        return path.substring(idx + 1);
    }
    return path;
}

size_t KOReaderDocumentId::getOffset(int i) {
    // Offset = 1024 << (2 * i)
    // For i = -1: KOReader LuaJIT bit.lshift(1024, -2) overflows to 0
    if (i < 0) {
        return 0;
    }
    return CHUNK_SIZE << (2 * i);
}

String KOReaderDocumentId::calculateFromFilename(const String& filePath) {
    String filename = getFilename(filePath);
    if (filename.length() == 0) return "";

    MD5Builder md5;
    md5.begin();
    md5.add(filename);
    md5.calculate();
    return md5.toString();
}

String KOReaderDocumentId::calculate(const String& filePath) {
    FsFile file = Storage.open(filePath.c_str(), O_RDONLY);
    if (!file) {
        Serial.printf("[KODoc] Failed to open file for MD5: %s\n", filePath.c_str());
        return "";
    }

    size_t fileSize = file.size();
    if (fileSize == 0) {
        file.close();
        return calculateFromFilename(filePath);
    }

    MD5Builder md5;
    md5.begin();

    uint8_t buffer[CHUNK_SIZE];
    size_t totalBytesRead = 0;

    // Read 1KB chunks at exponential offsets (i = -1 to 10)
    for (int i = -1; i < OFFSET_COUNT - 1; i++) {
        size_t offset = getOffset(i);
        if (offset >= fileSize) {
            continue;
        }

        if (!file.seek(offset)) {
            continue;
        }

        size_t bytesToRead = (fileSize - offset < CHUNK_SIZE) ? (fileSize - offset) : CHUNK_SIZE;
        int bytesRead = file.read(buffer, bytesToRead);
        if (bytesRead > 0) {
            md5.add(buffer, (size_t)bytesRead);
            totalBytesRead += bytesRead;
        }
    }

    file.close();
    md5.calculate();
    String docId = md5.toString();
    Serial.printf("[KODoc] Document ID for %s: %s (%u bytes hashed)\n", 
                  getFilename(filePath).c_str(), docId.c_str(), (unsigned int)totalBytesRead);
    return docId;
}
