#!/usr/bin/env python3
"""Write body paragraph text into an existing DOCX in place via stdin.

Each stdin line becomes the text of one body paragraph (`w:p`) in the order the
text extraction read them; blank lines become empty paragraphs, so the
round-trip preserves paragraph structure. The rest of the package -- styles,
settings, theme, fonts, headers, footers, footnotes, comments, media, and every
other part -- is kept byte-for-byte. Paragraph properties (`w:pPr`) are kept,
so paragraph-level formatting survives. Because the editor works on plain text
per paragraph, inline character formatting (e.g. bold or hyperlinks inside a
run) is intentionally not retained. The replacement is built in the system temp
directory and moved in place (or copied when the source sits on another volume).
Exit 0 writes; 1 reports a corrupt or non-DOCX package or a write failure; 2
reports a command-line error.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import zipfile
from xml.etree import ElementTree as ET

NAMESPACES = (
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
    "http://purl.oclc.org/ooxml/wordprocessingml/main",
)
MAIN = "word/document.xml"
XML_SPACE = "{http://www.w3.org/XML/1998/namespace}space"


def namespace(root: ET.Element) -> str:
    """Return the main XML namespace after checking its OOXML variant."""
    uri = root.tag[1:].split("}", 1)[0] if root.tag.startswith("{") else ""
    if uri not in NAMESPACES:
        raise ValueError(f"document uses unsupported XML namespace: {uri or '(none)'}")
    return "{" + uri + "}"


def run_text(w: str, text: str) -> ET.Element:
    """Build a run holding one plain-text `w:t`, preserving whitespace."""
    run = ET.Element(f"{w}r")
    t = ET.SubElement(run, f"{w}t")
    t.set(XML_SPACE, "preserve")
    t.text = text if text else None
    return run


def make_paragraph(w: str, text: str) -> ET.Element:
    """Build a new paragraph wrapping one plain-text run."""
    p = ET.Element(f"{w}p")
    p.append(run_text(w, text))
    return p


def set_text(p: ET.Element, w: str, text: str) -> None:
    """Replace a paragraph's text runs, keeping only its paragraph properties."""
    for child in list(p):
        if child.tag != f"{w}pPr":
            p.remove(child)
    p.append(run_text(w, text))


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: write_text.py DOCX\n")
        return 2
    path = sys.argv[1]
    text = sys.stdin.read().replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")
    try:
        with zipfile.ZipFile(path) as archive:
            entries: dict[str, tuple[zipfile.ZipInfo, bytes]] = {}
            for info in archive.infolist():
                entries[info.filename] = (info, archive.read(info.filename))
            doc_info, doc_bytes = entries[MAIN]
            root = ET.fromstring(doc_bytes)
    except (OSError, KeyError, ET.ParseError, zipfile.BadZipFile) as error:
        sys.stderr.write(f"invalid docx: {error}\n")
        return 1
    try:
        w = namespace(root)
        body = root.find(f"{w}body")
        if body is None:
            raise ValueError("document has no body")
        paragraphs = list(body.iter(f"{w}p"))
        for index, p in enumerate(paragraphs):
            set_text(p, w, lines[index] if index < len(lines) else "")
        extra = lines[len(paragraphs):]
        if extra:
            sect = body.find(f"{w}sectPr")
            for line in extra:
                new_p = make_paragraph(w, line)
                if sect is not None:
                    body.insert(list(body).index(sect), new_p)
                else:
                    body.append(new_p)
        new_doc = (
            '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            + ET.tostring(root, encoding="unicode")
        ).encode("utf-8")
    except ValueError as error:
        sys.stderr.write(f"invalid docx: {error}\n")
        return 1
    try:
        fd, tmp = tempfile.mkstemp(prefix="dshdoc-", suffix=".docx")
        os.close(fd)
    except OSError as error:
        sys.stderr.write(f"write failed: {error}\n")
        return 1
    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, (info, data) in entries.items():
                if name == MAIN:
                    continue
                copy = zipfile.ZipInfo(info.filename, info.date_time)
                copy.compress_type = info.compress_type
                copy.external_attr = info.external_attr
                archive.writestr(copy, data)
            # Overwrite document.xml with the edited body.
            archive.writestr(doc_info, new_doc)
        # A temp name appended beside a deep source path can exceed MAX_PATH, so
        # build the replacement in the system temp dir instead. When the source
        # lies on another volume an atomic rename is impossible; fall back to a
        # direct copy.
        try:
            os.replace(tmp, path)
        except OSError:
            shutil.copyfile(tmp, path)
            os.unlink(tmp)
    except OSError as error:
        sys.stderr.write(f"write failed: {error}\n")
        return 1
    finally:
        if os.path.exists(tmp):
            try:
                os.unlink(tmp)
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())