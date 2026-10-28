<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Cassian homepage

- `fonts/geist-latin.woff2` and `fonts/instrument-serif-latin.woff2` are the
  unmodified Latin font files from the owner's `cassian-next-blog` build cache
  (2026-10-26). Upstream families:
  [Geist](https://github.com/vercel/geist-font) and
  [Instrument Serif](https://github.com/Instrument/instrument-serif).
  Copyright notices and the SIL Open Font License 1.1 are in
  [`fonts/OFL.txt`](fonts/OFL.txt); generated subsets remain under that license.
- `fonts/cassian_display_28.c`: Geist at weight 600, 28 px, 4 bpp; uppercase
  Latin letters, digits, space, `.`, `-` and `%`. `fonts/cassian_editorial_18.c`:
  Instrument Serif regular at 18 px, 4 bpp; the letters and punctuation in
  `CF`, `Building things.`, `Mapping ideas.` and `Leaving traces.`.
- `fonts/jetbrains-mono-latin.woff2` is the unmodified JetBrains Mono variable
  Latin source, version 2.211, from the same personal-site build cache.
  Its upstream is [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono);
  the copyright notice and SIL Open Font License 1.1 are retained in
  [`fonts/JetBrainsMono-OFL.txt`](fonts/JetBrainsMono-OFL.txt).
  `fonts/cassian_mono_12.c`, `fonts/cassian_mono_14.c` and
  `fonts/cassian_mono_32.c` each contain 95 printable ASCII glyphs at weight
  400, 4 bpp, for instrument labels and numeric readings. Their bitmaps use
  21,228 Flash bytes altogether.
- [`../tools/generate_cassian_assets.py`](../tools/generate_cassian_assets.py)
  converts the retained sources to uncompressed LVGL 9 glyph bitmaps and a
  Flash-resident RGB565 portrait. Regeneration used fontTools 4.63.0 and
  sharp 0.35.5 with its bundled SVG renderer:

  ```bash
  python3 tools/generate_cassian_assets.py --sharp-module /path/to/node_modules/sharp
  ```

  These development dependencies are not needed to compile the generated C.
  The portrait contains 72,576 RGB565 bytes. The current Chinese application
  links Geist, the three monospaced subsets and the three UI subsets below;
  Instrument Serif remains a reusable asset but is not linked. Runtime LVGL
  rendering still uses its normal partial draw buffer and 32 KB pool, without
  an extra full-screen buffer. Native rendering passed 4,000 page transitions
  with a peak pool allocation of 18,928 bytes in the host configuration; this
  does not establish device heap headroom. Actual Chinese rendering, contrast
  and memory headroom remain on-device checks.

The application uses a graphite instrument layout with a fine grid, straight
menu borders, vector icons, lime selection and monospaced measurements. The
homepage keeps the mountain portrait; its double-OK gesture opens a separate
function list. The system gauge shows the free-heap fraction. Settings uses a
20-segment output meter, with one segment per 5% and no lit segments while
muted. The grid, icons and meter are drawn by page-owned LVGL callbacks without
per-line/per-segment objects, extra tasks or timers. Preview telemetry is
mocked; production readings come from the existing BSP and heap APIs.

### Simplified Chinese UI

- `fonts/cassian-ui-source.woff2` is a renamed subset of Noto Sans SC Regular,
  version `2.004-H2`, from the installed `@larksuite/whiteboard-cli` font assets.
  The original TTF SHA-256 is
  `5cf8b2a0576d5680284ab03a7a8219499d59bbe981a79bb3dc0031f251c39736`.
  Only the 22,220-byte subset is retained. Its copyright notice and SIL Open
  Font License 1.1 are in [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt);
  its family was renamed to Cassian UI while preserving the license metadata.
- `fonts/cassian-ui-characters.txt` records the 80 non-ASCII characters in the
  fixed UI. `fonts/cassian_ui_14.c` and `fonts/cassian_ui_18.c` each contain
  those characters plus printable ASCII: 175 glyphs at 14/18 px and 4 bpp.
  `fonts/cassian_ui_28.c` contains only U+9759 and U+97F3 for the large mute
  state. These are uncompressed LVGL 9 bitmap fonts, not arbitrary-name or
  network-text fonts. Unknown characters are unsupported; placeholders remain
  enabled so missing glyphs cannot silently disappear.
- The converter reads the Chinese string literals in `main/personal_home.c`.
  Regenerate from the retained source with the preceding command. When new
  characters are needed, supply the licensed full font so the source subset
  can also be refreshed. Use the same `2.004-H2` release: another release has
  slightly different outlines and would change every existing UI glyph.

  ```bash
  python3 tools/generate_cassian_assets.py \
    --sharp-module /path/to/node_modules/sharp \
    --chinese-source /path/to/NotoSansSC-Regular.ttf
  python3 tools/check_cassian_fonts.py
  ```

- The shared static gate checks the inventory, each generated font's coverage,
  a known absent U+9F98, bitmap bounds and uniform monospaced advance widths
  without development dependencies. Native LVGL verification additionally
  decodes each label's UTF-8, checks its actual selected font, layout and
  selected/muted/error states, and counts the volume meter's lit segments.
  Chinese UI bitmaps use 29,232 Flash bytes; with Geist and JetBrains Mono,
  linked fixed-UI font bitmaps total 57,891 bytes. Menu icons and the volume
  meter use LVGL drawing primitives, requiring no image decoder or icon-font
  fallback.

### AI conversation text

- `fonts/cassian_chat_14.c` renders transcripts, replies and every fixed string
  on the AI page (`main/ai_page.c`). It contains printable ASCII, all 7,445
  GB2312 characters (including the 6,763 hanzi) and the marks
  `— – … · “ ” ‘ ’`: 7,543 glyphs at 14 px and 4 bpp, with 679,301 bitmap bytes
  and about 0.75 MB of Flash including descriptors. Glyphs are read from Flash;
  the font adds no internal RAM. Characters outside this set, such as emoji or
  rare hanzi, are removed or drawn as placeholders.
- Source: Noto Sans SC Regular, an earlier `Version 0.00 July 30, 2017` build
  supplied by the device owner (TTF SHA-256
  `a0af66489baf1f306732ff16da58d86c342e0b3b4b3674079ee2a9b0aee23258`), under
  the same SIL Open Font License 1.1 in
  [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt). The 16 MB source is not
  committed. Regenerating this font alone leaves every other asset unchanged:

  ```bash
  python3 tools/generate_cassian_assets.py \
    --sharp-module /path/to/node_modules/sharp \
    --chat-source /path/to/NotoSansSC-Regular.ttf
  python3 tools/check_cassian_fonts.py
  ```

- The static gate checks full GB2312 coverage and that every fixed AI-page
  string is inside that set.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`images/cassian-avatar.png`](images/cassian-avatar.png) | 1024 × 1024, PNG | Owner-provided artwork from the local source of `cassianflorin.com`, used at the owner's request for this personal firmware. Retained unchanged; no separate third-party redistribution license was supplied. |
| [`images/cassian_portrait.c`](images/cassian_portrait.c) | 216 × 168, RGB565 C array | Display-sized center crop of the preceding artwork; generated by the shared Cassian asset converter and read directly from Flash. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).
