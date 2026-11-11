// Translates CFIs (one per line on stdin) with a CFI map: "pub" (converted EPUB
// -> web reader) or "loc" (back). Prints the result, "-" when not mapped.
#include "core/cfi_map.h"

#include <cstdio>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::string text;
    if (FILE* f = fopen(argv[1], "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        fclose(f);
    }
    CfiMap map;
    if (!map.parse(text.data(), text.size())) {
        fprintf(stderr, "not a CFI map\n");
        return 1;
    }
    const bool pub = std::string(argv[2]) == "pub";
    std::string line;
    while (std::getline(std::cin, line)) {
        EpubCfi::Location l;
        bool ok = EpubCfi::parse(line, l) && (pub ? map.toPublic(l) : map.toLocal(l));
        printf("%s\n", ok ? EpubCfi::format(l).c_str() : "-");
    }
    return 0;
}
