// JsonFileWriter (src/storage/json_writer.h): a document reaches the file whole,
// and a file that refuses bytes is reported instead of passing for a short copy.
#include <ArduinoJson.h>
#include <cstdio>
#include <string>

#include "storage/json_writer.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                               \
    do {                                                       \
        g_checks++;                                            \
        if (!(c)) {                                            \
            g_fail++;                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
        }                                                      \
    } while (0)

// A file that takes at most `limit` bytes, as a full card or a failing write.
struct FakeFile {
    std::string data;
    size_t limit = (size_t)-1;
    size_t calls = 0;
    size_t write(const void* p, size_t n) {
        calls++;
        size_t k = data.size() + n > limit ? (data.size() < limit ? limit - data.size() : 0) : n;
        data.append((const char*)p, k);
        return k;
    }
};

static void bigDoc(JsonDocument& doc, int books) {
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < books; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = i;
        o["title"] = "Сказания Меекханского пограничья. Небо цвета стали";
        o["author"] = "Роберт М. Вегнер";
    }
}

int main() {
    {   // a 60 KB catalog goes through in 512-byte blocks and matches the String form
        JsonDocument doc;
        bigDoc(doc, 300);
        std::string expected;
        serializeJson(doc, expected);
        FakeFile f;
        size_t n;
        bool ok;
        {
            JsonFileWriter<FakeFile> w(f);
            n = serializeJson(doc, w);
            ok = w.flush();
            CHECK(w.accepted() == measureJson(doc));
        }
        CHECK(ok);
        CHECK(n == measureJson(doc));
        CHECK(f.data == expected);
        CHECK(expected.size() > 40000);
        CHECK(f.calls >= expected.size() / 512);
        printf("  %u bytes in %u writes\n", (unsigned)f.data.size(), (unsigned)f.calls);
    }
    {   // pretty form (config.json)
        JsonDocument doc;
        doc["wifi_ssid"] = "дом";
        doc["frontlight"] = 40;
        std::string expected;
        serializeJsonPretty(doc, expected);
        FakeFile f;
        JsonFileWriter<FakeFile> w(f);
        const size_t n = serializeJsonPretty(doc, w);
        CHECK(w.flush());
        CHECK(n == measureJsonPretty(doc));
        CHECK(f.data == expected);
    }
    {   // the card refuses bytes part-way: failed, and the counts say short
        JsonDocument doc;
        bigDoc(doc, 300);
        FakeFile f;
        f.limit = 20000;
        bool ok;
        size_t n;
        {
            JsonFileWriter<FakeFile> w(f);
            n = serializeJson(doc, w);
            ok = w.flush() && w.accepted() == measureJson(doc);
            CHECK(w.failed());
        }
        CHECK(!ok);
        CHECK(n < measureJson(doc));
        CHECK(f.data.size() <= 20000);
    }
    {   // the last partial block fails only at flush (the writer's own buffer)
        JsonDocument doc;
        doc["a"] = 1;
        FakeFile f;
        f.limit = 2;
        JsonFileWriter<FakeFile> w(f);
        serializeJson(doc, w);
        CHECK(!w.flush());
        CHECK(w.failed());
    }
    printf("%s: %d/%d checks passed\n", g_fail ? "FAIL" : "OK", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
