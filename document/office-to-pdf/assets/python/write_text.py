#!/usr/bin/env python3
"""Rebuild a minimal body-only DOCX at OUTPUT.docx from paragraph text on stdin.

Each stdin line becomes one body run (`w:p` with a single `w:r/w:t`); blank lines
become empty paragraphs, so the extraction script's newline-joined body round-trips
paragraph structure losslessly. Content is XML-escaped and whitespace preserved.
Rich formatting, headers, footers, and non-paragraph content are not retained.
Exit 0 writes the package; 1 reports a write failure; 2 reports a command-line error.
"""

from __future__ import annotations

import sys
import zipfile
from xml.sax.saxutils import escape

NS_TYPES = "http://schemas.openxmlformats.org/package/2006/content-types"
NS_REL = "http://schemas.openxmlformats.org/package/2006/relationships"
NS_OFFICE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
NS_W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
CONTAINER = "application/vnd.openxmlformats-package.relationships+xml"
MAIN = "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: write_text.py OUTPUT.docx\n")
        return 2
    text = sys.stdin.read().replace("\r\n", "\n").replace("\r", "\n")
    paragraphs = [
        "<w:p/>" if not line else f'<w:p><w:r><w:t xml:space="preserve">{escape(line)}</w:t></w:r></w:p>'
        for line in text.split("\n")
    ]
    document = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<w:document xmlns:w="{NS_W}">\n'
        "  <w:body>\n"
        + "\n".join(f"    {paragraph}" for paragraph in paragraphs)
        + '\n  </w:body>\n</w:document>\n'
    )
    content_types = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<Types xmlns="{NS_TYPES}">\n'
        f'  <Default Extension="rels" ContentType="{CONTAINER}"/>\n'
        f'  <Default Extension="xml" ContentType="application/xml"/>\n'
        f'  <Override PartName="/word/document.xml" ContentType="{MAIN}"/>\n'
        "</Types>\n"
    )
    root_rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<Relationships xmlns="{NS_REL}">\n'
        f'  <Relationship Id="rId1" Type="{NS_OFFICE}/officeDocument" Target="word/document.xml"/>\n'
        "</Relationships>\n"
    )
    doc_rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<Relationships xmlns="{NS_REL}"/>\n'
    )
    try:
        with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("[Content_Types].xml", content_types)
            archive.writestr("_rels/.rels", root_rels)
            archive.writestr("word/document.xml", document)
            archive.writestr("word/_rels/document.xml.rels", doc_rels)
    except OSError as error:
        sys.stderr.write(f"write failed: {error}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())