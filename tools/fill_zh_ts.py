#!/usr/bin/env python3
"""Fill xtranslate_zh_CN.ts: the source language IS Chinese, so every
unfinished <translation> simply receives its <source> text verbatim.
Run after lupdate whenever new strings appear (idempotent)."""

import os
import sys
import xml.etree.ElementTree as ET

path = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "resources", "i18n", "xtranslate_zh_CN.ts")

tree = ET.parse(path)
root = tree.getroot()
filled = 0
for message in root.iter("message"):
    source = message.find("source")
    translation = message.find("translation")
    if source is None or translation is None:
        continue
    if translation.get("type") == "unfinished" and not (translation.text or "").strip():
        translation.text = source.text
        del translation.attrib["type"]
        filled += 1

tree.write(path, encoding="utf-8", xml_declaration=True)
# Re-add the DOCTYPE line lupdate/lrelease expect.
with open(path, "r", encoding="utf-8") as f:
    content = f.read()
if "<!DOCTYPE TS>" not in content:
    content = content.replace("?>", "?>\n<!DOCTYPE TS>", 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
print(f"filled {filled} translations in {path}")
