#!/usr/bin/env python3
"""Check fixed Chinese UI coverage and generated bitmap bounds without font tools."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def check_font(symbol, required, monospaced=False):
    source = (ROOT / "assets/fonts" / f"{symbol}.c").read_text()
    start = int(re.search(r"\.range_start = (\d+)", source)[1])
    offsets = re.search(r"unicode_offsets\[\] = \{([^}]+)\}", source)[1]
    coverage = {start + int(value) for value in offsets.split(",")}
    assert len(coverage) == int(re.search(r"\.list_length = (\d+)", source)[1])
    missing = sorted(set(map(ord, required)) - coverage)
    assert not missing, f"{symbol} missing: {[f'U+{c:04X}' for c in missing]}"
    # A known absent character proves that this is a bounded UI subset.
    assert 0x9F98 not in coverage, f"{symbol} unexpectedly claims arbitrary CJK"

    bitmap = re.search(r"bitmap\[\] = \{(.*?)\};", source, re.S)[1]
    length = len(re.findall(r"0x[0-9a-f]{2}", bitmap))
    glyphs = re.findall(
        r"\.bitmap_index = (\d+), \.adv_w = \d+, \.box_w = (\d+), \.box_h = (\d+)",
        source,
    )
    assert len(glyphs) == len(coverage)
    if monospaced:
        advances = set(re.findall(r"\.adv_w = (\d+)", source))
        assert len(advances) == 1, f"{symbol} is not monospaced"
    for index, (offset, width, height) in enumerate(glyphs):
        end = int(offset) + (int(width) * int(height) + 1) // 2
        boundary = int(glyphs[index + 1][0]) if index + 1 < len(glyphs) else length
        assert end <= boundary <= length, f"{symbol} glyph {index} exceeds its bitmap"
    print(f"{symbol}: PASS ({len(coverage)} glyphs, {length} bitmap bytes)")


def main():
    source = (ROOT / "main/personal_home.c").read_text()
    # This independent scan includes every non-ASCII character in the UI source,
    # so a new string cannot silently rely on an outdated generated inventory.
    required = {char for char in source if ord(char) > 127}
    retained = set((ROOT / "assets/fonts/cassian-ui-characters.txt").read_text().strip())
    assert required == retained, "UI text changed; regenerate the Chinese fonts"
    ascii_text = "".join(map(chr, range(32, 127)))
    for size in (14, 18):
        check_font(f"cassian_ui_{size}", ascii_text + "".join(required))
    check_font("cassian_ui_28", "\u9759\u97f3")
    for size in (12, 14, 32):
        check_font(f"cassian_mono_{size}", ascii_text, monospaced=True)

    # The AI page renders fixed and arbitrary text with the conversation font.
    page = (ROOT / "main/ai_page.c").read_text()
    gb2312 = set()
    for code in range(0x80, 0x10000):
        try:
            if len(chr(code).encode("gb2312")) == 2:
                gb2312.add(chr(code))
        except UnicodeEncodeError:
            pass
    page_text = {char for char in page if ord(char) > 127}
    assert page_text <= gb2312 | set("\u00b7"), "AI page text is outside the chat font"
    check_font("cassian_chat_14", ascii_text + "".join(gb2312 | page_text))


if __name__ == "__main__":
    main()
