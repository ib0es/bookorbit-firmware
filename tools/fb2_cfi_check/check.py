#!/usr/bin/env python3
"""Checks that CFIs of FB2 and AZW3 books converted by the firmware mean the same place
as the Book Orbit web reader's (foliate-js), both ways, with foliate itself.

  tools/fb2_cfi_check/check.py <foliate dir> <book.fb2>... [--every N]

Needs a C++ compiler, Node.js with Playwright and its Chromium. For each book:
the firmware's converter makes the EPUB and its CFI map; foliate lays the FB2
out in Chromium; points in the text are taken on each side, their CFIs carried
to the other side through the map (the firmware's CfiMap) and resolved there;
the text that follows must be the same.
"""
import json, os, re, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

def build(out):
    fib = os.path.join(ROOT, "freeink-sdk/libs/book/FreeInkBook")
    inc = ["-DFREEINK_BOOK_SMALL=1", "-I" + fib + "/include"] + ["-I%s/third_party/%s" % (fib, t) for t in
           ("expat", "miniz", "libunibreak", "pngle", "tjpgd", "stb")] + [
           "-I" + ROOT + "/freeink-sdk/libs/font/FreeInkFont/include",
           "-I" + ROOT + "/freeink-sdk/libs/font/FreeInkFont/third_party/stb"]
    flags = ["-std=c++17", "-O1", "-w", "-I" + ROOT + "/src", "-I" + ROOT + "/simulator/include",
             "-I" + ROOT + "/simulator/lib_arduinojson/src"]
    objs = []
    for src in ("miniz_impl", "expat_xmlparse", "expat_xmlrole", "expat_xmltok", "unibreak_impl", "pngle_impl", "tjpgd_impl"):
        o = os.path.join(out, src + ".o")
        subprocess.check_call(["cc", "-O1", "-w"] + inc + ["-c", fib + "/src/vendor/" + src + ".c", "-o", o])
        objs.append(o)
    core = [fib + "/src/" + s for s in ("FreeInkBook.cpp", "BookCatalog.cpp", "epub/ZipCatalog.cpp", "epub/XmlSax.cpp",
            "epub/PackageParsers.cpp", "epub/ImageProbe.cpp", "text/EntityFilter.cpp", "text/Hyphenator.cpp",
            "css/Css.cpp", "layout/ChapterLayout.cpp", "cache/PageCache.cpp", "render/ImageRenderer.cpp",
            "render/PageRenderer.cpp")] + [ROOT + "/freeink-sdk/libs/font/FreeInkFont/src/" + s for s in
            ("TtfFont.cpp", "Gpos.cpp", "Gsub.cpp")]
    subprocess.check_call(["c++"] + flags + inc + [HERE + "/fb2conv.cpp", ROOT + "/src/convert/fb2_to_epub.cpp", ROOT + "/src/convert/mobi_to_epub.cpp",
                           ROOT + "/src/convert/zip_writer.cpp"] + core + objs + ["-o", out + "/fb2conv"])
    subprocess.check_call(["c++"] + flags + [HERE + "/cfimap_cli.cpp", ROOT + "/src/reader/cfi_map.cpp",
                           ROOT + "/src/reader/epub_cfi.cpp", "-o", out + "/cfimap"])

def norm(t):
    return re.sub(r"[\s ]+", "", t or "")[:14]

def same(want, got):
    """The same text follows; a file of ours that ends there (the book goes on
    in the next one) gives less of it."""
    a, b = norm(want), norm(got)
    return a == b or (b != "" and len(b) < 14 and a.startswith(b))

def translate(tools, cfimap, way, cfis):
    r = subprocess.run([tools + "/cfimap", cfimap, way], input="\n".join(cfis) + "\n", capture_output=True, text=True,
                       check=True)
    return r.stdout.split("\n")[:len(cfis)]

def oracle(foliate, fb2, ours, job, work):
    of, jf, rf = work + "/ours.json", work + "/job.json", work + "/out.json"
    json.dump(ours, open(of, "w"))
    json.dump(job, open(jf, "w"))
    env = dict(os.environ)
    env.setdefault("PLAYWRIGHT_BROWSERS_PATH", "/opt/pw-browsers")
    env.setdefault("NODE_PATH", subprocess.run(["npm", "root", "-g"], capture_output=True, text=True).stdout.strip())
    subprocess.check_call(["node", HERE + "/oracle.mjs", foliate, fb2, of, jf, rf], env=env)
    return json.load(open(rf))

def check(foliate, fb2, tools, every):
    work = tempfile.mkdtemp()
    epub, cfimap = work + "/book.epub", work + "/book.cfimap"
    subprocess.check_call([tools + "/fb2conv", fb2, epub, cfimap], stdout=subprocess.DEVNULL)
    z = zipfile.ZipFile(epub)
    names = sorted(n for n in z.namelist() if re.match(r"OEBPS/c\d+\.xhtml$", n))
    ours = [z.read(n).decode("utf-8") for n in names]
    first = oracle(foliate, fb2, ours, {"sampleEvery": every}, work)
    theirs, mine = first["theirs"], first["ours"]
    to_ours = translate(tools, cfimap, "loc", [p["cfi"] for p in theirs])
    to_theirs = translate(tools, cfimap, "pub", [p["cfi"] for p in mine])
    second = oracle(foliate, fb2, ours, {"sampleEvery": 10**9, "resolveOurs": to_ours, "resolveTheirs": to_theirs}, work)
    bad = []
    for p, c, got in zip(theirs, to_ours, second["resolvedOurs"]):
        if not same(p["text"], got):
            bad.append(("web -> here", p["cfi"], c, p["text"], got))
    for p, c, got in zip(mine, to_theirs, second["resolvedTheirs"]):
        if not same(got, p["text"]):
            bad.append(("here -> web", p["cfi"], c, p["text"], got))
    n = len(theirs) + len(mine)
    print("%s (%s): %d sections, %d files, %d points, %d differ" % (os.path.basename(fb2), first.get("kind"), first["sections"], len(ours), n,
                                                              len(bad)))
    for b in bad[:12]:
        print("  %s %s -> %s\n    want %r\n    got  %r" % b)
    return not bad

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    every = 7
    if "--every" in sys.argv:
        every = int(sys.argv[sys.argv.index("--every") + 1])
        args.remove(str(every))
    if len(args) < 2:
        sys.exit(__doc__)
    tools = tempfile.mkdtemp()
    build(tools)
    ok = all([check(args[0], f, tools, every) for f in args[1:]])
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
