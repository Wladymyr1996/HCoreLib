#!/usr/bin/env python3
"""Folds WebUi/ into a single self-contained HTML file.

    python tools/packui.py WebUi/index.htm build/index.html
    python tools/packui.py WebUi/index.htm build/index.html --gzip

The device serves ONE file, so the stylesheet and the script are inlined and the
development-only panel is dropped. Nothing is minified beyond stripping comments
and blank lines: the saving is small next to gzip, and a released file that can
still be read is worth more than the bytes.

`--gzip` also writes `.gz` beside it, which is the form the firmware will embed -
esp_http_server can serve it with `Content-Encoding: gzip` untouched.

No third-party imports on purpose: this runs under the ESP-IDF venv.
"""

import argparse
import gzip
import os
import re
import sys


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def strip_css(text):
    """Drops /* ... */ comments and the blank lines they leave behind."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(line for line in text.splitlines() if line.strip())


def strip_js(text):
    """Drops /* ... */ and whole-line // comments.

    Deliberately conservative: it never touches a `//` that follows code, because
    telling a comment from the middle of a string or a regex literal needs a
    parser, and a packer that corrupts one line in a thousand is worse than one
    that saves fewer bytes.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    kept = []
    for line in text.splitlines():
        if line.lstrip().startswith("//"):
            continue
        if line.strip():
            kept.append(line)
    return "\n".join(kept)


def strip_html_comments(text):
    return re.sub(r"<!--.*?-->", "", text, flags=re.S)


def drop_dev_panel(text):
    """Removes the section that only exists when the page is opened as a file."""
    return re.sub(r'\s*<section class="panel panel--dev".*?</section>', "", text, flags=re.S)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", help="WebUi/index.htm")
    parser.add_argument("destination", help="where to write the single file")
    parser.add_argument("--gzip", action="store_true", help="also write destination.gz")
    parser.add_argument("--keep-dev", action="store_true",
                        help="keep the development device panel in the output")
    args = parser.parse_args()

    source_dir = os.path.dirname(os.path.abspath(args.source))
    html = read(args.source)

    # <link rel="stylesheet" href="..."> -> <style>...</style>
    def inline_css(match):
        path = os.path.join(source_dir, match.group(1))
        return "<style>\n%s\n</style>" % strip_css(read(path))

    html = re.sub(r'<link[^>]*rel="stylesheet"[^>]*href="([^"]+)"[^>]*>', inline_css, html)

    # <script src="..."></script> -> <script>...</script>
    def inline_js(match):
        path = os.path.join(source_dir, match.group(1))
        return "<script>\n%s\n</script>" % strip_js(read(path))

    html = re.sub(r'<script[^>]*src="([^"]+)"[^>]*></script>', inline_js, html)

    if not args.keep_dev:
        html = drop_dev_panel(html)

    html = strip_html_comments(html)
    html = "\n".join(line for line in html.splitlines() if line.strip())

    destination_dir = os.path.dirname(os.path.abspath(args.destination))
    if destination_dir and not os.path.isdir(destination_dir):
        os.makedirs(destination_dir)

    with open(args.destination, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(html)

    raw = len(html.encode("utf-8"))
    print("wrote %s (%d bytes)" % (args.destination, raw), file=sys.stderr)

    if args.gzip:
        packed = args.destination + ".gz"
        with open(packed, "wb") as handle:
            # mtime=0 so two builds of the same source produce identical bytes.
            with gzip.GzipFile(fileobj=handle, mode="wb", compresslevel=9, mtime=0) as zipped:
                zipped.write(html.encode("utf-8"))

        print("wrote %s (%d bytes, %.0f%% of raw)"
              % (packed, os.path.getsize(packed), 100.0 * os.path.getsize(packed) / raw),
              file=sys.stderr)


if __name__ == "__main__":
    main()
