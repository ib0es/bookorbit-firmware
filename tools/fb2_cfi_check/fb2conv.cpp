// Converts an FB2, MOBI or AZW3 file with the firmware's converter: book.epub and its CFI map.
// Usage: fb2conv in.fb2 out.epub out.cfimap
#include "convert/fb2_to_epub.h"
#include "convert/mobi_to_epub.h"

#include <cstdio>
#include <string>

#include "../../test/host/convert_test_util.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s in.fb2 out.epub out.cfimap\n", argv[0]);
        return 2;
    }
    std::string data;
    if (FILE* f = fopen(argv[1], "rb")) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
        fclose(f);
    } else {
        perror(argv[1]);
        return 1;
    }
    MemIn in(data);
    MemOut out;
    MemTemp tmp;
    convert::ConvertStats st;
    const std::string name = argv[1];
    const bool fb2 = name.size() > 4 && (name.compare(name.size() - 4, 4, ".fb2") == 0 || name.compare(name.size() - 4, 4, ".FB2") == 0);
    const convert::ConvertResult r = fb2 ? convert::fb2ToEpub(in, out, tmp, nullptr, nullptr, &st)
                                         : convert::mobiToEpub(in, out, tmp, nullptr, nullptr, &st);
    if (r != convert::ConvertResult::Ok) {
        fprintf(stderr, "convert: %s %s\n", convert::resultName(r), st.error.c_str());
        return 1;
    }
    FILE* e = fopen(argv[2], "wb");
    FILE* m = fopen(argv[3], "wb");
    if (!e || !m) return 1;
    fwrite(out.d.data(), 1, out.d.size(), e);
    fwrite(st.cfiMap.data(), 1, st.cfiMap.size(), m);
    fclose(e);
    fclose(m);
    printf("%u chapters\n", (unsigned)st.chapters);
    return 0;
}
