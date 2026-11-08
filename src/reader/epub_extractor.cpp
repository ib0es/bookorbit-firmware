#include "../hal/mem_stats.h"
#include "../ui/i18n.h"
#include "epub_extractor.h"
#include "html_stripper.h"
#include "miniz.h"
#include <map>
#include <vector>
#include <algorithm>

// Callback to read bytes from an open FsFile
static size_t zipReadCallback(void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n) {
    FsFile *f = (FsFile*)pOpaque;
    if (!f || !(*f)) return 0;
    if (!f->seek(file_ofs)) return 0;
    int bytesRead = f->read(pBuf, n);
    return (bytesRead > 0) ? (size_t)bytesRead : 0;
}

// Callback to write decompressed bytes to an open FsFile
static size_t zipWriteFileCallback(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    FsFile *f = (FsFile*)pOpaque;
    if (!f || !(*f)) return 0;
    return f->write((const uint8_t*)pBuf, n);
}

// Helper to safely extract a sub-string without allocating large heap buffers
static String extractSubString(const char* start, size_t len) {
    if (!start || len == 0) return "";
    if (len < 256) {
        char buf[256];
        memcpy(buf, start, len);
        buf[len] = '\0';
        return String(buf);
    } else {
        char* buf = (char*)malloc(len + 1);
        if (buf) {
            memcpy(buf, start, len);
            buf[len] = '\0';
            String res(buf);
            free(buf);
            return res;
        }
        return "";
    }
}

// Helper to URL-decode percent-encoded UTF-8 strings
static String urlDecode(const String& in) {
    String out = "";
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        if (in[i] == '%' && i + 2 < in.length()) {
            char h1 = in[i + 1];
            char h2 = in[i + 2];
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int v1 = hexVal(h1);
            int v2 = hexVal(h2);
            if (v1 >= 0 && v2 >= 0) {
                out += (char)((v1 << 4) | v2);
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

// Natural sort comparison for filenames with numbers (e.g. "ch1-2" before "ch1-10")
// Holds the miniz decompression workspace (~43 KB) for a whole EPUB operation, so
// it is allocated once instead of per chapter, and freed afterwards.
static bool s_lastFailedForMemory = false;

bool EpubExtractor::lastFailedForMemory() { return s_lastFailedForMemory; }

class MinizSession {
public:
    MinizSession() : _ok(miniz_c3_workspace_acquire()) {
        s_lastFailedForMemory = !_ok;
        if (!_ok) {
            Serial.printf("[EPUB] Not enough memory to unpack (%u bytes needed)\n", (unsigned)miniz_c3_workspace_size());
            MemStats::log("EPUB unpack failed");
        }
    }
    ~MinizSession() { if (_ok) miniz_c3_workspace_release(); }
    bool ok() const { return _ok; }
private:
    bool _ok;
};

static bool naturalCompare(const String& a, const String& b) {
    size_t i = 0, j = 0;
    while (i < a.length() && j < b.length()) {
        char ca = a[i];
        char cb = b[j];

        if (isdigit((unsigned char)ca) && isdigit((unsigned char)cb)) {
            unsigned long numA = 0;
            while (i < a.length() && isdigit((unsigned char)a[i])) {
                numA = numA * 10 + (a[i] - '0');
                i++;
            }
            unsigned long numB = 0;
            while (j < b.length() && isdigit((unsigned char)b[j])) {
                numB = numB * 10 + (b[j] - '0');
                j++;
            }
            if (numA != numB) {
                return numA < numB;
            }
        } else {
            char lca = tolower((unsigned char)ca);
            char lcb = tolower((unsigned char)cb);
            if (lca != lcb) {
                return lca < lcb;
            }
            i++;
            j++;
        }
    }
    return a.length() < b.length();
}

// Helper to find attribute value inside an XML/HTML tag string safely
static String findAttrValue(const char* tagStr, const char* attrName) {
    if (!tagStr || !attrName) return "";
    const char* endTag = strchr(tagStr, '>');
    size_t nameLen = strlen(attrName);
    const char* p = tagStr;

    while (*p) {
        if (endTag && p >= endTag) return "";
        p = strstr(p, attrName);
        if (!p) return "";
        if (endTag && p >= endTag) return "";

        // Verify boundary before attrName (must be space, delimiter, or '<')
        if (p > tagStr) {
            char prev = *(p - 1);
            if (prev != ' ' && prev != '\t' && prev != '\n' && prev != '\r' && prev != '<') {
                p += nameLen;
                continue;
            }
        }

        // Verify '=' follows attrName
        const char* eq = p + nameLen;
        while (*eq == ' ' || *eq == '\t' || *eq == '\r' || *eq == '\n') eq++;
        if (*eq != '=') {
            p += nameLen;
            continue;
        }

        // Found attribute name and '='! Now find start of value
        const char* val = eq + 1;
        while (*val == ' ' || *val == '\t' || *val == '\r' || *val == '\n') val++;
        if (!*val || (endTag && val >= endTag)) return "";

        if (*val == '"' || *val == '\'') {
            char quote = *val;
            val++;
            const char* endQuote = strchr(val, quote);
            if (!endQuote || (endTag && endQuote > endTag)) return "";
            return extractSubString(val, endQuote - val);
        } else {
            // Unquoted value
            const char* endVal = val;
            while (*endVal && *endVal != ' ' && *endVal != '\t' && *endVal != '\r' && *endVal != '\n' && *endVal != '>' && *endVal != '/') {
                endVal++;
            }
            return extractSubString(val, endVal - val);
        }
    }
    return "";
}

// StreamingHtmlStripper is defined in html_stripper.h

static size_t epubExtractHtmlCallback(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    StreamingHtmlStripper* stripper = (StreamingHtmlStripper*)pOpaque;
    stripper->processChunk((const uint8_t*)pBuf, n);
    return n;
}

bool EpubExtractor::isEpub(const String& filePath) {
    String lower = filePath;
    lower.toLowerCase();
    return lower.endsWith(".epub");
}

String EpubExtractor::getCachePath(const String& filePath) {
    int lastSlash = filePath.lastIndexOf('/');
    String baseName = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;
    int dotIdx = baseName.lastIndexOf('.');
    if (dotIdx > 0) {
        baseName = baseName.substring(0, dotIdx);
    }
    return String("/cache/") + baseName + ".txt";
}

String EpubExtractor::getCoverPath(const String& filePath) {
    int lastSlash = filePath.lastIndexOf('/');
    String baseName = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;
    int dotIdx = baseName.lastIndexOf('.');
    if (dotIdx > 0) {
        baseName = baseName.substring(0, dotIdx);
    }
    return String("/covers/") + baseName + ".jpg";
}

String EpubExtractor::getTocPath(const String& filePath) {
    int lastSlash = filePath.lastIndexOf('/');
    String baseName = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;
    int dotIdx = baseName.lastIndexOf('.');
    if (dotIdx > 0) {
        baseName = baseName.substring(0, dotIdx);
    }
    return String("/cache/") + baseName + ".toc";
}

String EpubExtractor::getSpinePath(const String& filePath) {
    int lastSlash = filePath.lastIndexOf('/');
    String baseName = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;
    int dotIdx = baseName.lastIndexOf('.');
    if (dotIdx > 0) {
        baseName = baseName.substring(0, dotIdx);
    }
    return String("/cache/") + baseName + ".spine";
}

String EpubExtractor::getCfiMapPath(const String& filePath) {
    String spine = getSpinePath(filePath);
    return spine.substring(0, spine.length() - 6) + ".cfimap";
}

static void saveCfiMap(const String& epubPath, const std::vector<EpubExtractor::CfiChapter>& map) {
    FsFile f = Storage.open(EpubExtractor::getCfiMapPath(epubPath).c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) return;
    f.printf("# CFIMAP_V1\n");
    for (const auto& c : map) f.printf("%d\t%d\t%s\n", c.spineIndex, c.ordinal, c.zipPath.c_str());
    f.close();
}

static String normalizeZipPath(const String& path) {
    String p = path;
    p.replace('\\', '/');
    while (p.startsWith("/")) p = p.substring(1);

    std::vector<String> parts;
    int start = 0;
    while (start < (int)p.length()) {
        int slash = p.indexOf('/', start);
        String seg = (slash >= 0) ? p.substring(start, slash) : p.substring(start);
        start = (slash >= 0) ? slash + 1 : p.length();
        if (seg.length() == 0 || seg == ".") continue;
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(seg);
        }
    }
    String result = "";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += "/";
        result += parts[i];
    }
    return result;
}

static int findFileInZip(mz_zip_archive* zip, const String& targetPath) {
    if (targetPath.length() == 0) return -1;
    String norm = normalizeZipPath(targetPath);

    int idx = mz_zip_reader_locate_file(zip, norm.c_str(), NULL, 0);
    if (idx >= 0) return idx;

    mz_uint numFiles = mz_zip_reader_get_num_files(zip);
    for (mz_uint i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(zip, i, &st)) {
            String fn(st.m_filename);
            fn.replace('\\', '/');
            if (norm.equalsIgnoreCase(fn)) return (int)i;
        }
    }

    int slash = norm.lastIndexOf('/');
    String bareName = (slash >= 0) ? norm.substring(slash + 1) : norm;
    if (bareName.length() > 0) {
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(zip, i, &st)) {
                String fn(st.m_filename);
                fn.replace('\\', '/');
                if (fn.endsWith("/" + bareName) || fn.equalsIgnoreCase(bareName)) {
                    return (int)i;
                }
            }
        }
    }
    return -1;
}

static int locateEpubCoverZipIndex(mz_zip_archive* zip, const String& opfPath, const String& baseDir) {
    String directImgHref = "";
    String wrapperHtmlHref = "";
    mz_uint numFiles = mz_zip_reader_get_num_files(zip);

    if (opfPath.length() > 0) {
        int opfIdx = findFileInZip(zip, opfPath);
        if (opfIdx >= 0) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(zip, opfIdx, &st) && st.m_uncomp_size > 0 && st.m_uncomp_size < 1048576) {
                size_t opfSize = (size_t)st.m_uncomp_size;
                char* opfData = (char*)malloc(opfSize + 1);
                if (opfData) {
                    if (mz_zip_reader_extract_to_mem(zip, opfIdx, opfData, opfSize, 0)) {
                        opfData[opfSize] = '\0';

                        // 1. Check <meta name="cover" content="..."/> or <meta content="..." name="cover"/>
                        String metaCoverId = "";
                        const char* pMetaCov = strstr(opfData, "name=\"cover\"");
                        if (!pMetaCov) pMetaCov = strstr(opfData, "name='cover'");
                        if (pMetaCov) {
                            const char* tagStart = pMetaCov;
                            while (tagStart > opfData && *tagStart != '<') tagStart--;
                            metaCoverId = findAttrValue(tagStart, "content");
                        } else {
                            const char* pCovName = strstr(opfData, "\"cover\"");
                            if (pCovName) {
                                const char* tagStart = pCovName;
                                while (tagStart > opfData && *tagStart != '<') tagStart--;
                                String nameVal = findAttrValue(tagStart, "name");
                                if (nameVal.equalsIgnoreCase("cover")) {
                                    metaCoverId = findAttrValue(tagStart, "content");
                                }
                            }
                        }

                        // 2. Scan manifest items
                        const char* pScan = opfData;
                        while (*pScan) {
                            const char* p1 = strstr(pScan, "<item");
                            const char* p2 = strstr(pScan, ":item");
                            const char* pItem = nullptr;
                            if (p1 && p2) pItem = (p1 < p2) ? p1 : p2;
                            else if (p1) pItem = p1;
                            else pItem = p2;
                            if (!pItem) break;

                            if (strncmp(pItem, "<itemref", 8) == 0 || strncmp(pItem, ":itemref", 8) == 0) {
                                pScan = pItem + 8;
                                continue;
                            }
                            char afterChar = pItem[5];
                            if (!isspace((unsigned char)afterChar) && afterChar != '/' && afterChar != '>') {
                                pScan = pItem + 5;
                                continue;
                            }

                            const char* endTag = strchr(pItem, '>');
                            if (!endTag) break;

                            const char* tagOpen = pItem;
                            while (tagOpen > opfData && *tagOpen != '<') tagOpen--;

                            String id = findAttrValue(tagOpen, "id");
                            String href = findAttrValue(tagOpen, "href");
                            String mType = findAttrValue(tagOpen, "media-type");
                            String props = findAttrValue(tagOpen, "properties");

                            if (href.length() > 0) {
                                href = urlDecode(href);
                                href.trim();
                                href.replace('\\', '/');
                                if (href.startsWith("/")) href = href.substring(1);

                                bool isImgType = (mType.indexOf("image/") >= 0) ||
                                                 href.endsWith(".jpg") || href.endsWith(".jpeg") ||
                                                 href.endsWith(".png") || href.endsWith(".bmp");
                                bool isHtmlType = (mType.indexOf("xhtml") >= 0) || (mType.indexOf("html") >= 0) ||
                                                  href.endsWith(".xhtml") || href.endsWith(".html") || href.endsWith(".htm");

                                bool isCoverProp = (props.indexOf("cover-image") >= 0);
                                bool isCoverId = (metaCoverId.length() > 0 && id.equals(metaCoverId));
                                bool isNamedCover = id.equalsIgnoreCase("cover-image") || id.equalsIgnoreCase("cover_image") ||
                                                    id.equalsIgnoreCase("cover") || id.equalsIgnoreCase("bookcover");

                                if (isCoverProp && isImgType) {
                                    directImgHref = href;
                                    break; // EPUB3 standard
                                } else if (isCoverId && isImgType) {
                                    directImgHref = href;
                                    break; // EPUB2 meta
                                } else if (isNamedCover && isImgType && directImgHref.length() == 0) {
                                    directImgHref = href;
                                } else if ((isCoverProp || isCoverId || isNamedCover) && isHtmlType && wrapperHtmlHref.length() == 0) {
                                    wrapperHtmlHref = href;
                                }
                            }
                            pScan = endTag + 1;
                        }

                        // 3. Scan <guide> for <reference type="cover" href="..."/>
                        if (directImgHref.length() == 0 && wrapperHtmlHref.length() == 0) {
                            const char* pGuide = strstr(opfData, "<guide");
                            if (pGuide) {
                                const char* pRef = strstr(pGuide, "type=\"cover\"");
                                if (!pRef) pRef = strstr(pGuide, "type='cover'");
                                if (pRef) {
                                    const char* refStart = pRef;
                                    while (refStart > pGuide && *refStart != '<') refStart--;
                                    String gHref = findAttrValue(refStart, "href");
                                    if (gHref.length() > 0) {
                                        int hashPos = gHref.indexOf('#');
                                        if (hashPos >= 0) gHref = gHref.substring(0, hashPos);
                                        gHref = urlDecode(gHref);
                                        gHref.trim();
                                        if (gHref.endsWith(".jpg") || gHref.endsWith(".jpeg") || gHref.endsWith(".png") || gHref.endsWith(".bmp")) {
                                            directImgHref = gHref;
                                        } else {
                                            wrapperHtmlHref = gHref;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    free(opfData);
                }
            }
        }
    }

    // A. Check direct image candidate
    if (directImgHref.length() > 0) {
        String fullPath = normalizeZipPath(baseDir + directImgHref);
        int idx = findFileInZip(zip, fullPath);
        if (idx >= 0) return idx;
    }

    // B. Check wrapper XHTML/HTML candidate
    if (wrapperHtmlHref.length() > 0) {
        String fullHtmlPath = normalizeZipPath(baseDir + wrapperHtmlHref);
        int htmlIdx = findFileInZip(zip, fullHtmlPath);
        if (htmlIdx >= 0) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(zip, htmlIdx, &st) && st.m_uncomp_size > 0 && st.m_uncomp_size < 131072) {
                size_t hSize = (size_t)st.m_uncomp_size;
                char* hData = (char*)malloc(hSize + 1);
                if (hData) {
                    if (mz_zip_reader_extract_to_mem(zip, htmlIdx, hData, hSize, 0)) {
                        hData[hSize] = '\0';
                        String foundImg = "";

                        const char* pImg = strstr(hData, "<image");
                        if (!pImg) pImg = strstr(hData, ":image");
                        if (!pImg) pImg = strstr(hData, "<img");
                        if (pImg) {
                            foundImg = findAttrValue(pImg, "xlink:href");
                            if (foundImg.length() == 0) foundImg = findAttrValue(pImg, "src");
                            if (foundImg.length() == 0) foundImg = findAttrValue(pImg, "href");
                        }

                        if (foundImg.length() > 0) {
                            foundImg = urlDecode(foundImg);
                            foundImg.trim();
                            int lastSlash = fullHtmlPath.lastIndexOf('/');
                            String htmlDir = (lastSlash >= 0) ? fullHtmlPath.substring(0, lastSlash + 1) : "";
                            String resolvedImg = normalizeZipPath(htmlDir + foundImg);
                            int imgIdx = findFileInZip(zip, resolvedImg);
                            if (imgIdx >= 0) {
                                free(hData);
                                return imgIdx;
                            }
                        }
                    }
                    free(hData);
                }
            }
        }
    }

    // C. Fallback heuristic scan over all ZIP files (CrossPoint style)
    int bestIdx = -1;
    int bestScore = 0;
    for (mz_uint i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(zip, i, &st) || st.m_uncomp_size < 500) continue;
        String fn(st.m_filename);
        String lower = fn;
        lower.toLowerCase();
        lower.replace('\\', '/');

        bool isJpg = lower.endsWith(".jpg") || lower.endsWith(".jpeg");
        bool isBmp = lower.endsWith(".bmp");
        if (!isJpg && !isBmp) continue;

        int score = 0;
        int lastSlash = lower.lastIndexOf('/');
        String filenameOnly = (lastSlash >= 0) ? lower.substring(lastSlash + 1) : lower;

        if (filenameOnly.startsWith("cover.") || filenameOnly.equals("cover.jpg") || filenameOnly.equals("cover.jpeg")) {
            score = 100;
        } else if (filenameOnly.indexOf("cover") >= 0) {
            score = 80;
        } else if (filenameOnly.indexOf("titlepage") >= 0 || filenameOnly.indexOf("title_page") >= 0) {
            score = 70;
        } else if (filenameOnly.indexOf("front") >= 0 || filenameOnly.indexOf("jacket") >= 0 || filenameOnly.indexOf("poster") >= 0) {
            score = 50;
        }

        if (score > bestScore) {
            bestScore = score;
            bestIdx = (int)i;
            if (score == 100) break;
        }
    }

    return bestIdx;
}

bool EpubExtractor::extractCoverOnly(const String& epubPath, const String& outCoverPath) {
    if (epubPath.length() == 0 || outCoverPath.length() == 0) return false;
    if (!Storage.isAvailable() || !Storage.exists(epubPath.c_str())) return false;
    MinizSession miniz;
    if (!miniz.ok()) return false;

    if (!Storage.exists("/covers")) {
        Storage.mkdir("/covers");
    }

    FsFile epubFile = Storage.open(epubPath.c_str(), O_RDONLY);
    if (!epubFile) return false;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zipReadCallback;
    zip.m_pIO_opaque = &epubFile;

    if (!mz_zip_reader_init(&zip, epubFile.size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        epubFile.close();
        return false;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    String opfPath = "";
    int containerIdx = mz_zip_reader_locate_file(&zip, "META-INF/container.xml", NULL, 0);
    if (containerIdx < 0) {
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&zip, i, &st)) {
                String fn(st.m_filename);
                if (fn.equalsIgnoreCase("META-INF/container.xml")) {
                    containerIdx = i;
                    break;
                }
            }
        }
    }

    if (containerIdx >= 0) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, containerIdx, &st) && st.m_uncomp_size < 32768) {
            size_t cSize = (size_t)st.m_uncomp_size;
            char* cXml = (char*)malloc(cSize + 1);
            if (cXml) {
                if (mz_zip_reader_extract_to_mem(&zip, containerIdx, cXml, cSize, 0)) {
                    cXml[cSize] = '\0';
                    const char* pRoot = strstr(cXml, "<rootfile");
                    if (!pRoot) pRoot = strstr(cXml, ":rootfile");
                    if (pRoot) {
                        opfPath = findAttrValue(pRoot, "full-path");
                    }
                }
                free(cXml);
            }
        }
    }

    if (opfPath.length() == 0) {
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&zip, i, &st)) {
                String fn(st.m_filename);
                if (fn.endsWith(".opf") || fn.endsWith(".OPF")) {
                    opfPath = fn;
                    break;
                }
            }
        }
    }

    String baseDir = "";
    if (opfPath.length() > 0) {
        int slash = opfPath.lastIndexOf('/');
        if (slash >= 0) baseDir = opfPath.substring(0, slash + 1);
    }

    int cZipIdx = locateEpubCoverZipIndex(&zip, opfPath, baseDir);
    bool success = false;

    if (cZipIdx >= 0) {
        FsFile coverOut = Storage.open(outCoverPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (coverOut) {
            mz_zip_reader_extract_to_callback(&zip, cZipIdx, zipWriteFileCallback, &coverOut, 0);
            size_t cSize = coverOut.size();
            coverOut.close();
            if (cSize > 300) {
                // Validate magic bytes
                FsFile ef = Storage.open(outCoverPath.c_str(), O_RDONLY);
                if (ef) {
                    uint8_t magic[2] = {0};
                    ef.read(magic, 2);
                    ef.close();
                    if ((magic[0] == 0xFF && magic[1] == 0xD8) || (magic[0] == 'B' && magic[1] == 'M')) {
                        Serial.printf("[EPUB] Cover successfully extracted (%u bytes) -> %s\n", (unsigned int)cSize, outCoverPath.c_str());
                        success = true;
                    } else {
                        Serial.printf("[EPUB] Extracted cover invalid magic (0x%02X 0x%02X), removing.\n", magic[0], magic[1]);
                        Storage.remove(outCoverPath.c_str());
                    }
                }
            } else {
                Storage.remove(outCoverPath.c_str());
            }
        }
    }

    mz_zip_reader_end(&zip);
    epubFile.close();
    return success;
}

// Reads META-INF/container.xml and the OPF: manifest, reading order (spine) with
// the <itemref> ordinal of each entry (EPUB CFI numbering; 0 = guessed order),
// cover and NCX. Shared by the text conversion and CFI resolution.
static void readPackage(mz_zip_archive& zip, mz_uint numFiles, String& opfPath, String& baseDir,
                        std::map<String, String>& manifest, std::vector<String>& spine,
                        std::vector<int>& spineOrdinals, String& coverHref, String& coverId, String& ncxHref) {
    opfPath = ""; baseDir = ""; coverHref = ""; coverId = ""; ncxHref = "";
    manifest.clear(); spine.clear(); spineOrdinals.clear();
    // 1. Locate and parse META-INF/container.xml to get OPF path
    int containerIdx = mz_zip_reader_locate_file(&zip, "META-INF/container.xml", NULL, 0);
    if (containerIdx < 0) {
        // Case-insensitive search
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&zip, i, &st)) {
                String fn(st.m_filename);
                if (fn.equalsIgnoreCase("META-INF/container.xml")) {
                    containerIdx = i;
                    break;
                }
            }
        }
    }

    if (containerIdx >= 0) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, containerIdx, &st) && st.m_uncomp_size < 32768) {
            size_t cSize = (size_t)st.m_uncomp_size;
            char* cXml = (char*)malloc(cSize + 1);
            if (cXml) {
                if (mz_zip_reader_extract_to_mem(&zip, containerIdx, cXml, cSize, 0)) {
                    cXml[cSize] = '\0';
                    const char* pRoot = strstr(cXml, "<rootfile");
                    if (!pRoot) pRoot = strstr(cXml, ":rootfile");
                    if (pRoot) {
                        opfPath = findAttrValue(pRoot, "full-path");
                    }
                }
                free(cXml);
            }
        }
    }

    // Fallback: search for any .opf file in the ZIP
    if (opfPath.length() == 0) {
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&zip, i, &st)) {
                String fn(st.m_filename);
                if (fn.endsWith(".opf") || fn.endsWith(".OPF")) {
                    opfPath = fn;
                    break;
                }
            }
        }
    }

    opfPath = urlDecode(opfPath);
    opfPath.trim();
    opfPath.replace('\\', '/');
    if (opfPath.startsWith("/")) {
        opfPath = opfPath.substring(1);
    }

    Serial.printf("[EPUB] OPF path: %s\n", opfPath.c_str());

    int opfSlash = opfPath.lastIndexOf('/');
    if (opfSlash >= 0) {
        baseDir = opfPath.substring(0, opfSlash + 1);
    }

    // 2. Parse OPF file: build manifest, spine, and identify NCX

    int opfIdx = -1;
    if (opfPath.length() > 0) {
        opfIdx = findFileInZip(&zip, opfPath);
    }

    if (opfIdx >= 0) {
        // Stream OPF to temporary file on SD to protect low internal SRAM on ESP32-C3
        String tmpOpfPath = "/cache/.opf.tmp";
        Storage.remove(tmpOpfPath.c_str());
        FsFile tmpOpf = Storage.open(tmpOpfPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (tmpOpf) {
            mz_zip_reader_extract_to_callback(&zip, opfIdx, zipWriteFileCallback, &tmpOpf, 0);
            tmpOpf.close();
        }

        FsFile opfFile = Storage.open(tmpOpfPath.c_str(), O_RDONLY);
        if (opfFile) {
            size_t opfSize = opfFile.size();
            char* opfData = (opfSize > 0 && opfSize < 131072) ? (char*)malloc(opfSize + 1) : nullptr;
            if (opfData) {
                opfFile.read((uint8_t*)opfData, opfSize);
                opfData[opfSize] = '\0';

                // Find cover ID in <meta name="cover" content="...">
                const char* pMetaCov = strstr(opfData, "name=\"cover\"");
                if (!pMetaCov) pMetaCov = strstr(opfData, "name='cover'");
                if (pMetaCov) {
                    const char* tagStart = pMetaCov;
                    while (tagStart > opfData && *tagStart != '<') tagStart--;
                    coverId = findAttrValue(tagStart, "content");
                }

                // Find spine toc attribute: <spine toc="..."> or <opf:spine toc="...">
                String spineTocId = "";
                const char* pSpineTag = strstr(opfData, "<spine");
                if (!pSpineTag) pSpineTag = strstr(opfData, ":spine");
                if (pSpineTag) {
                    spineTocId = findAttrValue(pSpineTag, "toc");
                }

                // Parse <manifest> items (supports both <item and <opf:item)
                const char* pScan = opfData;
                while (*pScan) {
                    const char* p1 = strstr(pScan, "<item");
                    const char* p2 = strstr(pScan, ":item");
                    const char* pItem = nullptr;
                    if (p1 && p2) pItem = (p1 < p2) ? p1 : p2;
                    else if (p1) pItem = p1;
                    else pItem = p2;
                    if (!pItem) break;

                    // Skip itemref
                    if (strncmp(pItem, "<itemref", 8) == 0 || strncmp(pItem, ":itemref", 8) == 0) {
                        pScan = pItem + 8;
                        continue;
                    }

                    // Verify tag boundary
                    char afterChar = (strncmp(pItem, "<item", 5) == 0) ? pItem[5] : pItem[5];
                    if (!isspace((unsigned char)afterChar) && afterChar != '/' && afterChar != '>') {
                        pScan = pItem + 5;
                        continue;
                    }

                    const char* endTag = strchr(pItem, '>');
                    if (!endTag) break;

                    const char* tagOpen = pItem;
                    while (tagOpen > opfData && *tagOpen != '<') tagOpen--;

                    String id = findAttrValue(tagOpen, "id");
                    String href = findAttrValue(tagOpen, "href");
                    String props = findAttrValue(tagOpen, "properties");
                    String mType = findAttrValue(tagOpen, "media-type");

                    if (id.length() > 0 && href.length() > 0) {
                        href = urlDecode(href);
                        href.trim();
                        href.replace('\\', '/');
                        if (href.startsWith("/")) href = href.substring(1);

                        bool isCover = id.equalsIgnoreCase("cover") || id.equalsIgnoreCase("cover-image") ||
                                       props.indexOf("cover-image") >= 0 || (coverId.length() > 0 && id.equals(coverId));
                        if (isCover) {
                            coverHref = href;
                        }
                        bool isNcx = id.equals(spineTocId) || mType.indexOf("x-dtbncx+xml") >= 0 || href.endsWith(".ncx");
                        if (isNcx) {
                            ncxHref = href;
                        }

                        bool isMediaOrFont = href.endsWith(".jpg") || href.endsWith(".jpeg") || href.endsWith(".png") ||
                                             href.endsWith(".gif") || href.endsWith(".ttf") || href.endsWith(".otf") ||
                                             href.endsWith(".woff") || href.endsWith(".css") || href.endsWith(".mp3");
                        if (!isMediaOrFont || isCover || isNcx) {
                            manifest[id] = href;
                        }
                    }
                    pScan = endTag + 1;
                }

                // Parse <spine> items (supports both <itemref and <opf:itemref)
                const char* pSpine = strstr(opfData, "<spine");
                if (!pSpine) pSpine = strstr(opfData, ":spine");
                if (pSpine) {
                    const char* pSpineEnd = strstr(pSpine, "</spine>");
                    if (!pSpineEnd) pSpineEnd = strstr(pSpine, ":spine>");
                    const char* pRef = pSpine;
                    int ordinal = 0;   // 1-based position among all <itemref>s (EPUB CFI numbering)
                    while (*pRef) {
                        const char* r1 = strstr(pRef, "<itemref");
                        const char* r2 = strstr(pRef, ":itemref");
                        const char* nextRef = nullptr;
                        if (r1 && r2) nextRef = (r1 < r2) ? r1 : r2;
                        else if (r1) nextRef = r1;
                        else nextRef = r2;

                        if (!nextRef || (pSpineEnd && nextRef >= pSpineEnd)) break;
                        const char* endRef = strchr(nextRef, '>');
                        if (!endRef) break;

                        const char* refOpen = nextRef;
                        while (refOpen > opfData && *refOpen != '<') refOpen--;

                        String idref = findAttrValue(refOpen, "idref");
                        ordinal++;
                        if (idref.length() > 0 && manifest.find(idref) != manifest.end()) {
                            String href = manifest[idref];
                            int hashPos = href.indexOf('#');
                            if (hashPos >= 0) href = href.substring(0, hashPos);
                            href = urlDecode(href);
                            href.trim();
                            href.replace('\\', '/');
                            if (href.startsWith("/")) href = href.substring(1);
                            spine.push_back(baseDir + href);
                            spineOrdinals.push_back(ordinal);
                        }
                        pRef = endRef + 1;
                    }
                }
                free(opfData);
            }
            opfFile.close();
            Storage.remove(tmpOpfPath.c_str());
        }
    }

    // 3. Fallback for spine if empty (uses natural sorting so ch1-2 comes before ch1-10)
    if (spine.empty()) {
        Serial.println("[EPUB] Spine empty, scanning archive for XHTML/HTML files with natural sort...");
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&zip, i, &st)) {
                String fn(st.m_filename);
                String lower = fn;
                lower.toLowerCase();
                if ((lower.endsWith(".xhtml") || lower.endsWith(".html") || lower.endsWith(".htm")) &&
                    lower.indexOf("toc") < 0) {
                    spine.push_back(fn);
                }
            }
        }
        std::sort(spine.begin(), spine.end(), naturalCompare);
        spineOrdinals.assign(spine.size(), 0);   // not from the OPF: no CFI numbering
    }

}

bool EpubExtractor::extractToText(const String& epubPath, const String& outTxtPath, String* outCoverPath) {
    Serial.printf("[EPUB] Starting extraction: %s -> %s\n", epubPath.c_str(), outTxtPath.c_str());
    MemStats::log("EPUB unpack start");
    MinizSession miniz;
    if (!miniz.ok()) return false;

    if (!Storage.exists("/cache")) {
        Storage.mkdir("/cache");
    }
    if (!Storage.exists("/covers")) {
        Storage.mkdir("/covers");
    }

    FsFile epubFile = Storage.open(epubPath.c_str(), O_RDONLY);
    if (!epubFile) {
        Serial.printf("[EPUB] Failed to open source file: %s\n", epubPath.c_str());
        return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zipReadCallback;
    zip.m_pIO_opaque = &epubFile;

    if (!mz_zip_reader_init(&zip, epubFile.size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        Serial.println("[EPUB] mz_zip_reader_init failed!");
        epubFile.close();
        return false;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    Serial.printf("[EPUB] ZIP archive opened successfully. Entries: %lu\n", (unsigned long)numFiles);

    String opfPath, baseDir, coverHref, coverId, ncxHref;
    std::map<String, String> manifest;
    std::vector<String> spine;
    std::vector<int> spineOrdinals;
    readPackage(zip, numFiles, opfPath, baseDir, manifest, spine, spineOrdinals, coverHref, coverId, ncxHref);

    Serial.printf("[EPUB] Ordered chapters found: %d\n", spine.size());
    if (spine.empty()) {
        Serial.println("[EPUB] No readable text chapters found in EPUB!");
        mz_zip_reader_end(&zip);
        epubFile.close();
        return false;
    }

    // 4. Parse Table of Contents from NCX if present
    std::map<String, String> tocMap;
    String ncxZipPath = "";
    if (ncxHref.length() > 0) {
        ncxHref = urlDecode(ncxHref);
        ncxHref.trim();
        ncxHref.replace('\\', '/');
        if (ncxHref.startsWith("/")) ncxHref = ncxHref.substring(1);
        ncxZipPath = baseDir + ncxHref;
    } else {
        // Fallback search for any .ncx in ZIP
        for (mz_uint i = 0; i < numFiles; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&zip, i, &st)) {
                String fn(st.m_filename);
                if (fn.endsWith(".ncx") || fn.endsWith(".NCX")) {
                    ncxZipPath = fn;
                    break;
                }
            }
        }
    }

    if (ncxZipPath.length() > 0) {
        int ncxZipIdx = findFileInZip(&zip, ncxZipPath);
        if (ncxZipIdx >= 0) {
            String tmpNcxPath = "/cache/.ncx.tmp";
            Storage.remove(tmpNcxPath.c_str());
            FsFile tmpNcx = Storage.open(tmpNcxPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
            if (tmpNcx) {
                mz_zip_reader_extract_to_callback(&zip, ncxZipIdx, zipWriteFileCallback, &tmpNcx, 0);
                tmpNcx.close();
            }

            FsFile ncxFile = Storage.open(tmpNcxPath.c_str(), O_RDONLY);
            if (ncxFile) {
                size_t ncxSize = ncxFile.size();
                char* ncxData = (ncxSize > 0 && ncxSize < 131072) ? (char*)malloc(ncxSize + 1) : nullptr;
                if (ncxData) {
                    ncxFile.read((uint8_t*)ncxData, ncxSize);
                    ncxData[ncxSize] = '\0';

                    const char* pNav = ncxData;
                    while ((pNav = strstr(pNav, "<navPoint")) != nullptr) {
                        const char* pNavEnd = strstr(pNav, "</navPoint>");
                        const char* pNextNav = strstr(pNav + 9, "<navPoint");
                        const char* blockEnd = pNavEnd;
                        if (pNextNav && (!pNavEnd || pNextNav < pNavEnd)) {
                            blockEnd = pNextNav;
                        }
                        if (!blockEnd) blockEnd = pNav + strlen(pNav);

                        String title = "";
                        const char* pText = strstr(pNav, "<text");
                        if (pText && pText < blockEnd) {
                            const char* textClose = strchr(pText, '>');
                            if (textClose && textClose < blockEnd) {
                                const char* textEnd = strstr(textClose, "</text>");
                                if (textEnd && textEnd < blockEnd) {
                                    title = extractSubString(textClose + 1, textEnd - (textClose + 1));
                                    title.trim();
                                }
                            }
                        }

                        const char* pContent = strstr(pNav, "<content");
                        String src = "";
                        if (pContent && pContent < blockEnd) {
                            src = findAttrValue(pContent, "src");
                        }

                        if (src.length() > 0 && title.length() > 0) {
                            int hashPos = src.indexOf('#');
                            if (hashPos >= 0) src = src.substring(0, hashPos);
                            src = urlDecode(src);
                            src.trim();
                            src.replace('\\', '/');
                            if (src.startsWith("/")) src = src.substring(1);
                            tocMap[baseDir + src] = title;
                            tocMap[src] = title;
                        }

                        pNav = blockEnd;
                    }
                    Serial.printf("[EPUB] Loaded %d chapters from NCX Table of Contents.\n", (int)tocMap.size());
                    free(ncxData);
                }
                ncxFile.close();
                Storage.remove(tmpNcxPath.c_str());
            }
        }
    }

    // 5. Extract embedded Cover image if available
    String coverDstPath = getCoverPath(epubPath);
    bool hasValidCover = false;
    if (Storage.exists(coverDstPath.c_str())) {
        FsFile ef = Storage.open(coverDstPath.c_str(), O_RDONLY);
        if (ef) {
            if (ef.size() > 300) {
                uint8_t magic[2] = {0};
                ef.read(magic, 2);
                if ((magic[0] == 0xFF && magic[1] == 0xD8) || (magic[0] == 'B' && magic[1] == 'M')) {
                    hasValidCover = true;
                }
            }
            ef.close();
            if (!hasValidCover) {
                Storage.remove(coverDstPath.c_str());
            }
        }
    }

    if (!hasValidCover) {
        int cZipIdx = locateEpubCoverZipIndex(&zip, opfPath, baseDir);
        if (cZipIdx >= 0) {
            FsFile coverOut = Storage.open(coverDstPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
            if (coverOut) {
                mz_zip_reader_extract_to_callback(&zip, cZipIdx, zipWriteFileCallback, &coverOut, 0);
                size_t cSize = coverOut.size();
                coverOut.close();
                if (cSize > 300) {
                    FsFile ef = Storage.open(coverDstPath.c_str(), O_RDONLY);
                    if (ef) {
                        uint8_t magic[2] = {0};
                        ef.read(magic, 2);
                        ef.close();
                        if ((magic[0] == 0xFF && magic[1] == 0xD8) || (magic[0] == 'B' && magic[1] == 'M')) {
                            hasValidCover = true;
                            if (outCoverPath) *outCoverPath = coverDstPath;
                            Serial.printf("[EPUB] Cover successfully extracted (%u bytes) -> %s\n", (unsigned int)cSize, coverDstPath.c_str());
                        } else {
                            Storage.remove(coverDstPath.c_str());
                            Serial.printf("[EPUB] Extracted cover invalid magic (0x%02X 0x%02X), removed.\n", magic[0], magic[1]);
                        }
                    }
                } else {
                    Storage.remove(coverDstPath.c_str());
                    Serial.println("[EPUB] Cover image was empty or too small, removed.");
                }
            }
        }
    } else {
        if (outCoverPath) *outCoverPath = coverDstPath;
    }

    // 6. Ensure cache directory exists and open output cache file directly
    Storage.mkdir("/cache");
    FsFile outFile = Storage.open(outTxtPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!outFile) {
        Serial.printf("[EPUB] Failed to create output cache file: %s\n", outTxtPath.c_str());
        mz_zip_reader_end(&zip);
        epubFile.close();
        return false;
    }

    // 7. Extract and strip HTML for each chapter in spine, collecting TOC offsets
    StreamingHtmlStripper stripper(&outFile);
    std::vector<ExtractedChapter> chapterTOC;
    struct SpineRecordEntry {
        int spineIndex;
        uint32_t byteOffset;
        String title;
    };
    std::vector<SpineRecordEntry> spineRecords;

    for (size_t ch = 0; ch < spine.size(); ch++) {
        yield();
        vTaskDelay(1); // Give FreeRTOS scheduler time to feed TWDT

        String chPath = spine[ch];
        chPath = urlDecode(chPath);
        chPath.trim();
        chPath.replace('\\', '/');
        int chZipIdx = mz_zip_reader_locate_file(&zip, chPath.c_str(), NULL, 0);

        if (chZipIdx < 0) {
            for (mz_uint i = 0; i < numFiles; i++) {
                mz_zip_archive_file_stat st;
                if (mz_zip_reader_file_stat(&zip, i, &st)) {
                    String zfn(st.m_filename);
                    zfn.replace('\\', '/');
                    if (chPath.equalsIgnoreCase(zfn)) {
                        chZipIdx = i;
                        break;
                    }
                }
            }
        }

        if (chZipIdx < 0) {
            int slash = chPath.lastIndexOf('/');
            String bareName = (slash >= 0) ? chPath.substring(slash + 1) : chPath;
            for (mz_uint i = 0; i < numFiles; i++) {
                mz_zip_archive_file_stat st;
                if (mz_zip_reader_file_stat(&zip, i, &st)) {
                    String zfn(st.m_filename);
                    zfn.replace('\\', '/');
                    if (zfn.endsWith("/" + bareName) || zfn.equalsIgnoreCase(bareName)) {
                        chZipIdx = i;
                        break;
                    }
                }
            }
        }

        if (chZipIdx >= 0) {
            stripper.flush();
            if (ch > 0) {
                stripper.writeByte('\f'); // Form feed page break between spine chapters
                stripper.flush();
            }
            uint32_t chByteOffset = outFile.position();
            stripper.resetForChapter();

            mz_zip_reader_extract_to_callback(&zip, chZipIdx, epubExtractHtmlCallback, &stripper, 0);
            stripper.emitNewline(2);
            stripper.flush();

            // Determine chapter title
            String chTitle = "";
            if (tocMap.find(chPath) != tocMap.end()) {
                chTitle = tocMap[chPath];
            } else {
                int lastSlash = chPath.lastIndexOf('/');
                String subPath = (lastSlash >= 0) ? chPath.substring(lastSlash + 1) : chPath;
                if (tocMap.find(subPath) != tocMap.end()) {
                    chTitle = tocMap[subPath];
                }
            }

            if (chTitle.length() == 0) {
                chTitle = stripper.getFirstHeading();
            }

            if (chTitle.length() > 0) {
                chapterTOC.push_back({ chTitle, chByteOffset });
            } else if (chapterTOC.empty()) {
                chapterTOC.push_back({ tr(MsgId::READER_TOC_START), chByteOffset });
            }
            char secBuf[32];
            snprintf(secBuf, sizeof(secBuf), tr(MsgId::READER_TOC_SECTION_FMT), (int)(ch + 1));
            spineRecords.push_back({ (int)ch, chByteOffset, chTitle.length() > 0 ? chTitle : String(secBuf) });
        } else {
            Serial.printf("[EPUB] Warning: chapter file not found in ZIP: %s\n", chPath.c_str());
        }
    }

    stripper.flush();
    outFile.flush();
    size_t outSize = outFile.size();
    if (outSize == 0) outSize = outFile.position();
    outFile.close();
    mz_zip_reader_end(&zip);
    epubFile.close();

    if (outSize == 0) {
        Serial.println("[EPUB] Extracted cache file is empty!");
        Storage.remove(outTxtPath.c_str());
        return false;
    }

    // 8. Write extracted Table of Contents to /cache/<book_name>.toc (with V2 format header)
    String tocPath = getTocPath(epubPath);
    FsFile tocFile = Storage.open(tocPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (tocFile) {
        tocFile.printf("%s\n", "# TOC_V2");
        for (const auto& c : chapterTOC) {
            tocFile.printf("%u\t%s\n", (unsigned int)c.byteOffset, c.title.c_str());
        }
        tocFile.close();
        Serial.printf("[EPUB] Saved %d chapters to TOC cache (V2): %s\n", (int)chapterTOC.size(), tocPath.c_str());
    }

    // 9. Write spine index for exact KOReader DocFragment[N] navigation to /cache/<book_name>.spine
    String spinePath = getSpinePath(epubPath);
    FsFile spineFile = Storage.open(spinePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (spineFile) {
        spineFile.printf("%s\n", "# SPINE_V1");
        for (const auto& sp : spineRecords) {
            spineFile.printf("%d\t%u\t%s\n", sp.spineIndex, (unsigned int)sp.byteOffset, sp.title.c_str());
        }
        spineFile.close();
        Serial.printf("[EPUB] Saved %d spine chapters for KOReader sync: %s\n", (int)spineRecords.size(), spinePath.c_str());
    }

    // 10. CFI map: <itemref> number and EPUB file of each chapter (web reader positions).
    std::vector<CfiChapter> cfiMap;
    for (const auto& sp : spineRecords) {
        cfiMap.push_back({ sp.spineIndex, spineOrdinals[sp.spineIndex], spine[sp.spineIndex] });
    }
    saveCfiMap(epubPath, cfiMap);

    Serial.printf("[EPUB] Extraction complete! Cache size: %u bytes\n", (unsigned int)outSize);
    return true;
}

// ============================================================================
// EPUB CFI
// ============================================================================
namespace {

// Counts the text the HTML stripper would produce for the chapter (as in the cache).
class StripperSink : public EpubCfi::TextSink {
public:
    StripperSink() : _stripper(nullptr, /*countOnly=*/true) { _stripper.resetForChapter(); }
    void feed(const char* d, size_t n) override { _stripper.processChunk((const uint8_t*)d, n); }
    uint32_t emitted() const override { return _stripper.emitted; }
private:
    StreamingHtmlStripper _stripper;
};

size_t walkerCallback(void* opaque, mz_uint64, const void* buf, size_t n) {
    EpubCfi::Walker* w = (EpubCfi::Walker*)opaque;
    w->feed((const char*)buf, n);
    return w->done() ? 0 : n;   // 0 stops the extraction early
}

// Streams one file of the EPUB through `walker`.
bool walkZipEntry(const String& epubPath, const String& zipPath, EpubCfi::Walker& walker) {
    const uint32_t t0 = millis();
    FsFile f = Storage.open(epubPath.c_str(), O_RDONLY);
    if (!f) return false;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zipReadCallback;
    zip.m_pIO_opaque = &f;
    bool ok = false;
    if (mz_zip_reader_init(&zip, f.size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        const int idx = findFileInZip(&zip, zipPath);
        MinizSession miniz;
        if (idx >= 0 && miniz.ok()) {
            mz_zip_reader_extract_to_callback(&zip, (mz_uint)idx, walkerCallback, &walker, 0);
            walker.finish();
            ok = true;
        }
        mz_zip_reader_end(&zip);
    }
    f.close();
    const uint32_t ms = millis() - t0;
    if (!ok || ms > 500) Serial.printf("[CFI] walk %s: %s in %lu ms\n", zipPath.c_str(), ok ? "done" : "failed", (unsigned long)ms);
    return ok;
}

}  // namespace

bool EpubExtractor::loadCfiMap(const String& epubPath, std::vector<CfiChapter>& out) {
    out.clear();
    const String mapPath = getCfiMapPath(epubPath);
    FsFile f = Storage.open(mapPath.c_str(), O_RDONLY);
    if (f) {
        char line[300];
        while (f.available()) {
            size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            if (n == 0 || line[0] == '#') continue;
            char* t1 = strchr(line, '\t');
            char* t2 = t1 ? strchr(t1 + 1, '\t') : nullptr;
            if (!t2) continue;
            *t1 = *t2 = '\0';
            out.push_back({ atoi(line), atoi(t1 + 1), String(t2 + 1) });
        }
        f.close();
        if (!out.empty()) return true;
    }

    // Cache made before CFI support: rebuild the map from the OPF.
    const uint32_t t0 = millis();
    Serial.println("[EPUB] CFI map: rebuilding from the OPF...");
    FsFile epub = Storage.open(epubPath.c_str(), O_RDONLY);
    if (!epub) return false;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zipReadCallback;
    zip.m_pIO_opaque = &epub;
    if (mz_zip_reader_init(&zip, epub.size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        Serial.printf("[EPUB] CFI map: zip opened (%lu ms)\n", (unsigned long)(millis() - t0));
        MinizSession miniz;
        if (miniz.ok()) {
            String opfPath, baseDir, coverHref, coverId, ncxHref;
            std::map<String, String> manifest;
            std::vector<String> spine;
            std::vector<int> ordinals;
            readPackage(zip, mz_zip_reader_get_num_files(&zip), opfPath, baseDir, manifest, spine, ordinals,
                        coverHref, coverId, ncxHref);
            for (size_t i = 0; i < spine.size(); i++) out.push_back({ (int)i, ordinals[i], spine[i] });
        }
        mz_zip_reader_end(&zip);
    }
    epub.close();
    Serial.printf("[EPUB] CFI map: %u chapters in %lu ms\n", (unsigned)out.size(), (unsigned long)(millis() - t0));
    if (!out.empty()) saveCfiMap(epubPath, out);
    return !out.empty();
}

bool EpubExtractor::cfiToChapterOffset(const String& epubPath, const String& zipPath, const EpubCfi::Location& loc,
                                       uint32_t& outOffset, bool* reached) {
    StripperSink sink;
    EpubCfi::Walker w = EpubCfi::Walker::toOffset(sink, loc.steps, loc.offset);
    if (!walkZipEntry(epubPath, zipPath, w)) return false;
    outOffset = w.textOffset();
    if (reached) *reached = w.reached();
    return true;
}

bool EpubExtractor::chapterOffsetToCfi(const String& epubPath, const String& zipPath, uint32_t offset,
                                       EpubCfi::Location& out) {
    StripperSink sink;
    EpubCfi::Walker w = EpubCfi::Walker::toLocation(sink, offset);
    if (!walkZipEntry(epubPath, zipPath, w) || !w.reached()) return false;
    out = w.location();
    return true;
}
