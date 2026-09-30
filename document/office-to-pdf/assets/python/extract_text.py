#!/usr/bin/env python3
"""Extract the body paragraph text of a DOCX for an inline text editor.

Reads INPUT.docx and writes one JSON object ``{"text": "..."}`` to stdout where
``text`` is each body ``w:p`` paragraph's ``w:t`` text joined by newlines.
Headers, footers, footnotes, comments, and non-paragraph content (tables' cells,
drawings) are ignored because the paired writer rebuilds the document solely
from these body paragraphs. Exit 0 extracts text; 1 reports a corrupt or
non-DOCX package; 2 reports a command-line error.
"""

from __future__ import annotations

import json
import sys
import zipfile
from xml.etree import ElementTree as ET

NAMESPACES = (
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
    "http://purl.oclc.org/ooxml/wordprocessingml/main",
)
MAIN = "word/document.xml"


def namespace(root: ET.Element) -> str:
    """Return the main XML namespace after checking its OOXML variant."""
    uri = root.tag[1:].split("}", 1)[0] if root.tag.startswith("{") else ""
    if uri not in NAMESPACES:
        raise ValueError(f"document uses unsupported XML namespace: {uri or '(none)'}")
    return "{" + uri + "}"


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: extract_text.py INPUT.docx\n")
        return 2
    try:
        with zipfile.ZipFile(sys.argv[1]) as archive:
            root = ET.fromstring(archive.read(MAIN))
    except (OSError, KeyError, ET.ParseError, zipfile.BadZipFile) as error:
        sys.stderr.write(f"invalid docx: {error}\n")
        return 1
    try:
        w = namespace(root)
        body = root.find(f"{w}body")
        if body is None:
            raise ValueError("document has no body")
        paragraphs = [
            "".join(node.text or "" for node in paragraph.iter(f"{w}t"))
            for paragraph in body.iter(f"{w}p")
        ]
    except ValueError as error:
        sys.stderr.write(f"invalid docx: {error}\n")
        return 1
    sys.stdout.write(json.dumps({"text": "\n".join(paragraphs)}, ensure_ascii=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())