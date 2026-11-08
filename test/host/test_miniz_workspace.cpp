// Host test of the miniz workspace patch (src/reader/miniz.c): the ~43 KB
// decompression workspace exists only while something extracts, sessions hold
// it across calls, and a failed allocation fails cleanly.  Run: test/host/run.sh
// Linux only (uses ld --wrap=malloc to simulate an exhausted heap).
#include "reader/miniz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Allocations of exactly the workspace size fail while g_failWorkspace is set.
static bool g_failWorkspace = false;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n) {
    if (g_failWorkspace && n == miniz_c3_workspace_size()) return nullptr;
    return __real_malloc(n);
}

static size_t collect(void* opaque, mz_uint64, const void* buf, size_t n) {
    static_cast<std::string*>(opaque)->append(static_cast<const char*>(buf), n);
    return n;
}

int main(int argc, char** argv) {
    const char* epub = argc > 1 ? argv[1] : "simulator/sdcard/books/Alice in Wonderland.epub";
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, epub, 0)) { printf("cannot open %s\n", epub); return 1; }
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    printf("workspace %u bytes, %u files in %s\n", (unsigned)miniz_c3_workspace_size(), n, epub);
    CHECK(miniz_c3_workspace_size() > 40000);
    CHECK(!miniz_c3_workspace_in_use());

    // Single calls: allocated during the call only; callback and heap paths agree.
    size_t total = 0;
    bool same = true, freedAfterEach = true;
    for (mz_uint i = 0; i < n; i++) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        std::string viaCallback;
        if (!mz_zip_reader_extract_to_callback(&zip, i, collect, &viaCallback, 0)) { same = false; continue; }
        freedAfterEach &= !miniz_c3_workspace_in_use();
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        freedAfterEach &= !miniz_c3_workspace_in_use();
        same &= p && sz == viaCallback.size() && memcmp(p, viaCallback.data(), sz) == 0;
        mz_free(p);
        total += sz;
    }
    printf("extracted %u bytes\n", (unsigned)total);
    CHECK(total > 100000);
    CHECK(same);
    CHECK(freedAfterEach);

    // A session keeps one workspace across calls; nested holders are counted.
    CHECK(miniz_c3_workspace_acquire());
    CHECK(miniz_c3_workspace_acquire());
    std::string s;
    CHECK(mz_zip_reader_extract_to_callback(&zip, n - 1, collect, &s, 0));
    CHECK(miniz_c3_workspace_in_use());
    miniz_c3_workspace_release();
    CHECK(miniz_c3_workspace_in_use());
    miniz_c3_workspace_release();
    CHECK(!miniz_c3_workspace_in_use());
    miniz_c3_workspace_release();   // extra release is harmless
    CHECK(!miniz_c3_workspace_in_use());

    // No memory: acquire and extraction fail cleanly, nothing stays allocated,
    // and everything works again once memory is back.
    g_failWorkspace = true;
    CHECK(!miniz_c3_workspace_acquire());
    std::string none;
    CHECK(!mz_zip_reader_extract_to_callback(&zip, n - 1, collect, &none, 0));
    CHECK(mz_zip_get_last_error(&zip) == MZ_ZIP_ALLOC_FAILED);
    char small[16];
    CHECK(!mz_zip_reader_extract_to_mem(&zip, n - 1, small, sizeof(small), 0));
    CHECK(!miniz_c3_workspace_in_use());
    g_failWorkspace = false;
    std::string again;
    CHECK(mz_zip_reader_extract_to_callback(&zip, n - 1, collect, &again, 0) && again == s);

    mz_zip_reader_end(&zip);
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
