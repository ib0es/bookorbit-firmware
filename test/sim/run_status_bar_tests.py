#!/usr/bin/env python3
"""Run real status-bar rendering against a generated, redistributable EPUB."""

import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "test/sim/build-x4/status_bar_tests"
    with tempfile.TemporaryDirectory(prefix="status-bars-") as root:
        book = Path(root) / "status.epub"
        with zipfile.ZipFile(book, "w") as archive:
            archive.writestr("mimetype", "application/epub+zip")
            archive.writestr(
                "META-INF/container.xml",
                '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                '<rootfiles><rootfile full-path="package.opf" media-type="application/oebps-package+xml"/>'
                '</rootfiles></container>',
            )
            archive.writestr(
                "package.opf",
                '<package version="3.0" unique-identifier="id" xmlns="http://www.idpf.org/2007/opf">'
                '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                '<dc:identifier id="id">status-bars-test</dc:identifier>'
                '<dc:title>A deliberately long book title for status bar regression tests</dc:title>'
                '<dc:language>en</dc:language></metadata><manifest>'
                '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
                + "".join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(3))
                + '</manifest><spine>'
                + "".join(f'<itemref idref="c{i}"/>' for i in range(3))
                + '</spine></package>',
            )
            archive.writestr(
                "nav.xhtml",
                '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
                '<head><title>Contents</title></head><body><nav epub:type="toc"><ol>'
                + "".join(f'<li><a href="c{i}.xhtml">Chapter {i + 1}: a long chapter title</a></li>' for i in range(3))
                + '</ol></nav></body></html>',
            )
            for i in range(3):
                archive.writestr(
                    f"c{i}.xhtml",
                    '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter</title></head><body>'
                    + f'<h1>Chapter {i + 1}</h1>'
                    + '<p>Sample book text for pagination and status bar geometry checks.</p>' * 100
                    + '</body></html>',
                )
        result = subprocess.run(
            [os.path.abspath(binary), root, "/status.epub"], capture_output=True, text=True, timeout=240
        )
        print("\n".join(line for line in result.stdout.splitlines() if line.startswith("Status bars:")))
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            print(result.stdout[-5000:], file=sys.stderr)
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
