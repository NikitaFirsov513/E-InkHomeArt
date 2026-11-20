# Home Art Sans Bitmap

Firmware bitmap subset derived from Noto Sans by `tools/generate_ui_font.py`.
Redistributed under SIL Open Font License 1.1, retained in `OFL.txt`.
Only ASCII, Russian Cyrillic, degree sign and middle dot are included.
The bitmap derivative is named **Home Art Sans Bitmap**.

Upstream: Google Fonts, `ofl/notosans/NotoSans[wdth,wght].ttf`.
Commit: `8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`.
SHA-256: `bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d`.

[Pinned font source](https://raw.githubusercontent.com/google/fonts/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf)

Firmware builds do not download fonts or require Pillow. For regeneration,
download the pinned source to `build/references/NotoSans.ttf`, then run:

```powershell
python tools/generate_ui_font.py --font build/references/NotoSans.ttf
```
