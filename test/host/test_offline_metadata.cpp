#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "services/offline_metadata.h"

class Source : public freeink::book::BookSource {
public:
    std::vector<uint8_t> bytes;
    explicit Source(const std::string& xml) : bytes(xml.begin(), xml.end()) {}
    int32_t readAt(uint64_t p, void* dst, uint32_t n) override {
        if (p >= bytes.size()) return 0;
        n = std::min<uint64_t>(n, bytes.size() - p);
        memcpy(dst, bytes.data() + p, n);
        return n;
    }
    uint64_t size() const override { return bytes.size(); }
};
static void be32(std::vector<uint8_t>& v, size_t p, uint32_t x) {
    for (int i = 3; i >= 0; --i) {
        v[p + i] = x & 255;
        x >>= 8;
    }
}
static Source epub(const std::string& metadata) {
    Source out("");
    auto& v = out.bytes;
    auto word = [&](uint32_t n, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) v.push_back(n >> (8 * i));
    };
    struct Entry {
        std::string name, text;
        uint32_t offset, crc;
    };
    std::vector<Entry> entries = {{"META-INF/container.xml",
                                   "<container><rootfiles><rootfile full-path='book.opf'/></rootfiles></container>", 0,
                                   0},
                                  {"book.opf",
                                   "<package><metadata><title>Title</title><creator>Ada</creator>" + metadata +
                                           "</metadata></package>",
                                   0, 0}};
    for (auto& e : entries) {
        e.offset = v.size();
        e.crc = 0xffffffff;
        for (unsigned char c : e.text) {
            e.crc ^= c;
            for (int b = 0; b < 8; ++b) e.crc = (e.crc >> 1) ^ (0xedb88320u & (0u - (e.crc & 1)));
        }
        e.crc ^= 0xffffffff;
        word(0x04034b50, 4);
        word(20, 2);
        word(0, 2);
        word(0, 2);
        word(0, 4);
        word(e.crc, 4);
        word(e.text.size(), 4);
        word(e.text.size(), 4);
        word(e.name.size(), 2);
        word(0, 2);
        v.insert(v.end(), e.name.begin(), e.name.end());
        v.insert(v.end(), e.text.begin(), e.text.end());
    }
    const uint32_t directory = v.size();
    for (auto& e : entries) {
        word(0x02014b50, 4);
        word(20, 2);
        word(20, 2);
        word(0, 2);
        word(0, 2);
        word(0, 4);
        word(e.crc, 4);
        word(e.text.size(), 4);
        word(e.text.size(), 4);
        word(e.name.size(), 2);
        word(0, 2);
        word(0, 2);
        word(0, 2);
        word(0, 2);
        word(0, 4);
        word(e.offset, 4);
        v.insert(v.end(), e.name.begin(), e.name.end());
    }
    const uint32_t length = v.size() - directory;
    word(0x06054b50, 4);
    word(0, 4);
    word(2, 2);
    word(2, 2);
    word(length, 4);
    word(directory, 4);
    word(0, 2);
    return out;
}
int main(int argc, char** argv) {
    Source fb2(
            "<?xml version='1.0'?><FictionBook><description><title-info><book-title>Embedded &amp; "
            "title</book-title><author><first-name>Ada</first-name><last-name>Lovelace</last-name></author></title-info></"
            "description><body/></FictionBook>");
    auto r = OfflineMetadata::extract(fb2, "/offline/opaque.fb2");
    assert(r.title == "Embedded & title");
    assert(r.author == "Ada Lovelace");
    assert(OfflineMetadata::extract(fb2, "/offline/plain.txt").title == "plain");
    for (const char* number : {"1.5", "", "NaN", "-1", "1x", "1e2"}) {
        Source sequence(
                std::string("<FictionBook><description><title-info><sequence name='Цикл &amp; Saga' number='") +
                number +
                "'/></title-info><src-title-info><sequence name='Wrong' number='9'/></src-title-info></description></FictionBook>");
        const auto series = OfflineMetadata::extract(sequence, "/offline/sequence.fb2");
        assert(series.series == "Цикл & Saga");
        assert(series.seriesIndex == (std::string(number) == "1.5" ? 1.5 : 0));
    }
    assert(r.series.length() == 0 && r.seriesIndex == 0);
    for (const auto& name : {std::string(510, 'a') + "Ц", std::string(511, 'a') + "Ц", std::string(509, 'a') + "😀"}) {
        Source sequence("<FictionBook><description><title-info><sequence name='" + name +
                        "' number='1.5'/></title-info></description></FictionBook>");
        const auto series = OfflineMetadata::extract(sequence, "/offline/bounded.fb2");
        const std::string expected = name.size() == 512 ? name : name.substr(0, name.find_first_not_of('a'));
        assert(series.series == expected.c_str() && series.seriesIndex == 1.5);
    }
    for (const char* metadata : {"<sequence number='1.5'/>", "<sequence name='' number='1.5'/>", ""}) {
        Source sequence(
                std::string("<FictionBook><description><title-info>") + metadata +
                "</title-info><src-title-info><sequence name='Wrong'/></src-title-info></description></FictionBook>");
        const auto series = OfflineMetadata::extract(sequence, "/offline/absent.fb2");
        assert(!series.series.length() && series.seriesIndex == 0);
    }
    {
        Source sequence(
                "<FictionBook><description><title-info><sequence name='First' number='1.5'/><sequence name='Second' number='2'/></title-info></description></FictionBook>");
        const auto series = OfflineMetadata::extract(sequence, "/offline/conflicting.fb2");
        assert(series.series == "First" && series.seriesIndex == 1.5);
    }
    const std::string calibre =
            "<meta name='calibre:series' content='Calibre'/><meta name='calibre:series_index' content='2.5'/>";
    const std::string collection =
            "<meta refines='#s' property='group-position'>1.5</meta><meta refines='#s' property='collection-type'>series</meta><meta id='s' property='belongs-to-collection'>Цикл &amp; Saga</meta>";
    for (const auto& metadata : {collection, calibre + collection, collection + calibre}) {
        auto book = epub(metadata);
        const auto series = OfflineMetadata::extract(book, "/offline/test.epub");
        assert(series.series == "Цикл & Saga" && series.seriesIndex == 1.5);
    }
    for (const char* number : {"", "NaN", "-1", "1x", "1e2", "1000001", "Infinity"}) {
        auto book = epub("<meta name='calibre:series' content='Calibre'/><meta name='calibre:series_index' content='" +
                         std::string(number) + "'/>");
        const auto series = OfflineMetadata::extract(book, "/offline/test.epub");
        assert(series.series == "Calibre" && series.seriesIndex == 0);
    }
    for (const auto& metadata :
         {std::string(),
          std::string(
                  "<meta property='belongs-to-collection' id='s'>Not a series</meta><meta refines='#s' property='collection-type'>set</meta>"),
          std::string("<meta name='calibre:series_index' content='1.5'/>"),
          std::string("<meta property='belongs-to-collection'>Unrefined</meta>")}) {
        auto book = epub(metadata);
        const auto series = OfflineMetadata::extract(book, "/offline/test.epub");
        assert(!series.series.length() && series.seriesIndex == 0);
    }
    {
        auto book = epub(calibre);
        const auto series = OfflineMetadata::extract(book, "/offline/test.epub");
        assert(series.series == "Calibre" && series.seriesIndex == 2.5);
    }
    Source malformed("<FictionBook><description><title-info><book-title>Wrong</book-title>");
    assert(OfflineMetadata::extract(malformed, "/offline/broken.fb2").title == "broken");
    Source entity(
            "<!DOCTYPE FictionBook [<!ENTITY x "
            "'untrusted'>]><FictionBook><description><title-info><book-title>&x;</book-title></title-info></description></"
            "FictionBook>");
    assert(OfflineMetadata::extract(entity, "/offline/entities.fb2").title == "entities");
    Source empty(
            "<FictionBook><description><title-info><book-title>  </book-title></title-info></description></FictionBook>");
    assert(OfflineMetadata::extract(empty, "/offline/empty.fb2").title == "empty");
    assert(OfflineMetadata::extract(fb2, "/offline/cancel.fb2", [] { return true; }).title == "cancel");
    Source mobi("");
    mobi.bytes.resize(512);
    memcpy(mobi.bytes.data() + 60, "BOOKMOBI", 8);
    mobi.bytes[77] = 2;
    be32(mobi.bytes, 78, 96);
    be32(mobi.bytes, 86, 500);
    memcpy(mobi.bytes.data() + 112, "MOBI", 4);
    be32(mobi.bytes, 116, 232);
    be32(mobi.bytes, 124, 65001);
    be32(mobi.bytes, 180, 300);
    be32(mobi.bytes, 184, 9);
    memcpy(mobi.bytes.data() + 396, "Real book", 9);
    be32(mobi.bytes, 224, 0x40);
    memcpy(mobi.bytes.data() + 344, "EXTH", 4);
    be32(mobi.bytes, 348, 23);
    be32(mobi.bytes, 352, 1);
    be32(mobi.bytes, 356, 100);
    be32(mobi.bytes, 360, 11);
    memcpy(mobi.bytes.data() + 364, "Ada", 3);
    for (const char* ext : {"mobi", "azw3", "azw"}) {
        r = OfflineMetadata::extract(mobi, String("/offline/opaque.") + ext);
        assert(r.title == "Real book" && r.author == "Ada");
        assert(!r.series.length() && r.seriesIndex == 0);
    }
    be32(mobi.bytes, 180, 0xfffffff0);
    assert(OfflineMetadata::extract(mobi, "/offline/corrupt.mobi").title == "corrupt");
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        Source epub("");
        epub.bytes.assign(std::istreambuf_iterator<char>(f), {});
        assert(!epub.bytes.empty());
        r = OfflineMetadata::extract(epub, "/offline/opaque.epub");
        assert(r.title != "opaque" && r.title.length() > 0);
        assert(r.author.length() > 0);
    }
    // The generated fixtures above always run; real books are local and optional in CI.
    if (argc > 2 && std::filesystem::is_directory(argv[2]))
        for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
            std::ifstream f(entry.path(), std::ios::binary);
            Source book("");
            book.bytes.assign(std::istreambuf_iterator<char>(f), {});
            r = OfflineMetadata::extract(book, String(entry.path().string().c_str()));
            assert(r.title != OfflineMetadata::fallbackTitle(String(entry.path().string().c_str())));
            assert(r.author.length() > 0);
        }
    puts("Offline metadata: all passed (28 series fixtures, 3 Palm formats without series)");
}
