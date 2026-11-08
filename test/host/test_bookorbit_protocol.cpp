// Host unit tests for the Book Orbit wire format.  Run: test/host/run.sh
// Fixtures are real responses of a Book Orbit server (2026-11).
#include "net/bookorbit_protocol.h"
#include "services/time_utils.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace BookOrbitProtocol;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// GET /api/v1/books/1/progress after the web reader moved (bare array, CFI, no xpath).
static const char* kRealWebProgress = R"J([
 {"fileId":1,"cfi":"epubcfi(/6/34!/4/2[id12]/2/2/1:0)","pageNumber":1980,"positionSeconds":null,
  "mediaOverlayFragment":null,"mediaOverlaySectionIndex":null,"percentage":20.41188,
  "koboLocationSource":"OPS/ch1-15.xhtml","koboLocationType":null,"koboLocationValue":null,
  "koboContentSourceProgressPercent":0,"koreaderProgress":null,"updatedAt":"2026-11-07T11:37:40.613Z"}
])J";

static void testBookProgress() {
    printf("parseBookProgress\n");
    Progress p;
    CHECK(parseBookProgress(kRealWebProgress, p));
    CHECK(p.exists);
    CHECK(near(p.percent, 0.2041188f));
    CHECK(p.page == 1980);
    CHECK(p.cfi == "epubcfi(/6/34!/4/2[id12]/2/2/1:0)");
    CHECK(p.updatedAt == TimeUtils::parseIso8601("2026-11-07T11:37:40Z"));

    CHECK(parseBookProgress("[]", p) && !p.exists);
    CHECK(parseBookProgress("{\"items\":[]}", p) && !p.exists);
    CHECK(parseBookProgress("{}", p) && !p.exists);
    CHECK(!parseBookProgress("<html>502 Bad Gateway</html>", p) && !p.exists);
    CHECK(!parseBookProgress("", p));

    // {"items":[...]} form, the most recent record wins regardless of order.
    CHECK(parseBookProgress(R"J({"items":[
        {"percentage":10,"updatedAt":"2026-11-07T10:00:00Z","koreaderProgress":"/body/DocFragment[3]/body/p[2]/text().0"},
        {"percentage":55.5,"updatedAt":"2026-11-07T12:00:00.5Z","text":{"page":42},"koreader":"/body/DocFragment[9]/body/p[1]/text().4"},
        {"percentage":30,"updatedAt":"2026-11-07T11:00:00Z"}]})J", p));
    CHECK(near(p.percent, 0.555f) && p.page == 42 && p.cfi.empty());

    // Records without a percentage are not positions; clamping; missing timestamps keep the first.
    CHECK(parseBookProgress(R"J([{"fileId":1,"cfi":"x","updatedAt":"2026-11-07T13:00:00Z"},{"percentage":150}])J", p));
    CHECK(p.exists && near(p.percent, 1.0f) && p.updatedAt == 0);
    CHECK(parseBookProgress(R"J([{"percentage":-5},{"percentage":40}])J", p) && near(p.percent, 0.0f));
    CHECK(parseBookProgress(R"J([{"percentage":12,"pageNumber":7,"cfi":null}])J", p));
    CHECK(p.page == 7 && p.cfi.empty());   // null, not "null"
    // The write response nests the CFI under "text".
    CHECK(parseBookProgress(R"J([{"percentage":12,"text":{"cfi":"epubcfi(/6/8!/4/2/1:0)","page":null}}])J", p));
    CHECK(p.cfi == "epubcfi(/6/8!/4/2/1:0)" && p.page == 0);
    CHECK(parseBookProgress(R"J([{"percentage":12,"updatedAt":"garbage"}])J", p) && p.exists && p.updatedAt == 0);
}

static void testLogin() {
    printf("parseLoginToken\n");
    std::string t;
    CHECK(parseLoginToken(R"J({"accessToken":"a.b.c","user":{"id":1}})J", t) && t == "a.b.c");
    CHECK(parseLoginToken(R"J({"token":"t1"})J", t) && t == "t1");
    CHECK(parseLoginToken(R"J({"access_token":"t2"})J", t) && t == "t2");
    CHECK(parseLoginToken(R"J({"data":{"token":"t3"}})J", t) && t == "t3");
    CHECK(parseLoginToken(R"J({"data":{"accessToken":"t4"}})J", t) && t == "t4");
    CHECK(!parseLoginToken(R"J({"accessToken":""})J", t) && t.empty());
    CHECK(!parseLoginToken(R"J({"accessToken":42})J", t));
    CHECK(!parseLoginToken("not json", t));
}

static void testWrite() {
    printf("buildProgressWrite / parseProgressWriteUpdatedAt\n");
    // With a CFI the reader's page number is not sent (the web reader numbers pages differently).
    CHECK(buildProgressWrite(7, 0.25f, 12, "epubcfi(/6/34!/4/2/2/2/1:0)") ==
          R"J({"writes":[{"fileId":7,"percentage":25,"text":{"cfi":"epubcfi(/6/34!/4/2/2/2/1:0)"}}]})J");
    CHECK(buildProgressWrite(7, 0.25f, 12) == R"J({"writes":[{"fileId":7,"percentage":25,"text":{"page":12}}]})J");
    CHECK(buildProgressWrite(7, 0.0f, 0) == R"J({"writes":[{"fileId":7,"percentage":0}]})J");
    CHECK(buildProgressWrite(7, 1.0f, 0) == R"J({"writes":[{"fileId":7,"percentage":100}]})J");
    CHECK(buildProgressWrite(7, 55.0f, 0) == R"J({"writes":[{"fileId":7,"percentage":55}]})J");   // already 0..100
    CHECK(buildProgressWrite(7, 500.0f, 0) == R"J({"writes":[{"fileId":7,"percentage":100}]})J");
    CHECK(buildProgressWrite(7, -0.5f, 0) == R"J({"writes":[{"fileId":7,"percentage":0}]})J");

    const uint32_t t = TimeUtils::parseIso8601("2026-11-07T11:00:00Z");
    CHECK(parseProgressWriteUpdatedAt(R"J({"results":[{"progress":{"updatedAt":"2026-11-07T11:00:00.000Z"}}]})J") == t);
    CHECK(parseProgressWriteUpdatedAt(R"J({"progress":{"updatedAt":"2026-11-07T11:00:00Z"}})J") == t);
    CHECK(parseProgressWriteUpdatedAt(R"J({"updatedAt":"2026-11-07T11:00:00Z"})J") == t);
    CHECK(parseProgressWriteUpdatedAt(R"J({"ok":true})J") == 0);
    CHECK(parseProgressWriteUpdatedAt("") == 0);
}

int main() {
    testBookProgress();
    testLogin();
    testWrite();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
