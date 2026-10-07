# wShell branding

Original identity for this repository. Teal `#3AA99F`, parchment `#CECDC3`, and charcoal `#100F0F` follow the public Flexoki palette. Assets are distributed under the root MIT license.

| Asset | Purpose |
| --- | --- |
| `wshell-mark.png` | Transparent standalone symbol |
| `wshell-wordmark.png` | Transparent app header and README lockup |
| `wshell-icon.png` | Rounded tile for Windows executable identity |
| `../wshell.ico` | Embedded ICO with 16, 24, 32, 48, 64, 128 and 256 pixel frames |

The PNG artwork was generated with OpenAI's built-in `image_gen` tool on 2026-10-07. The mark was the reference image for the wordmark and icon. No Termius images or fonts were used. PNG-to-ICO format conversion and downscaling are reproducible with `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-icon.ps1`; the original PNG remains intact.

## Mark prompt

```text
Use case: logo-brand. Create a polished, original app icon for a lightweight premium Windows SSH terminal application named wShell. Asset type: production application icon, transparent PNG, square 1024 x 1024. A compact beautiful geometric monogram: a lowercase w flowing into a terminal prompt chevron and a short cursor stroke, with a subtle layered shell / folded ribbon idea. Single cohesive symbol, not separate letters. Contemporary restrained developer-tool identity, flat vector-like crisp silhouette, gently rounded joins, excellent recognition at 24 px. Flexoki Dark palette: primarily muted teal #3AA99F and a very small warm parchment #CECDC3 accent, using deep charcoal #100F0F only if necessary within the symbol. No gradients, no bevels, no fake 3D, no shiny effects. Strong purposeful geometry and generous negative space within the glyph. Center the symbol with 12% clear padding around it. TRUE TRANSPARENT BACKGROUND, no background card or container. No words or lettering other than the abstract w-like symbol, no slogan, no watermark. This is a new original identity, do not imitate any existing terminal brand logo.
```

## Wordmark prompt

```text
Use case: logo-brand. Create a clean production horizontal brand lockup for the desktop SSH client wShell, using the attached image as the logo reference. Preserve the teal flowing w/terminal chevron symbol and parchment accent. Place a compact version of that mark on the left, with the exact text "wShell" to its right. Typography: refined modern geometric sans, medium to semibold, beautifully kerned, warm parchment #CECDC3. Render the spelling and capitalization exactly: lowercase w, uppercase S, lowercase h e l l. No other text. Horizontal 3:1 composition, icon and wordmark balanced optically on the same central baseline, generous 8% outer margins but tight clean lockup. Designed to look excellent on a near-black #100F0F interface. Background must be truly transparent with intact alpha. Flat crisp logo artwork only, no scene, no shadows, no badges, no mockups, no textures, no watermark. This PNG will be used directly as an app header image and README brand banner.
```

## Executable icon prompt

```text
Use case: logo-brand. Create a production Windows application icon variant for wShell using the attached logo as the identity reference. Keep the recognizable muted teal flowing w into a terminal chevron and small warm parchment cursor motif. Adapt it for excellent legibility at small Windows taskbar sizes: simplify tiny details, use thicker clean silhouettes and a compact centered arrangement. Put the symbol on one deep Flexoki charcoal #100F0F rounded square tile with subtle #343331 border, occupying 88% of the square canvas, with a fully transparent outer area. Symbol color #3AA99F and tiny #CECDC3 accent. The mark should occupy 82% of the tile width, optically centered. Square 1024x1024. Flat polished premium developer-tool aesthetic, crisp vector-like edges, absolutely no gritty texture, no white edge fringes, no glow, no shadows, no 3D, no additional text or letters. Preserve the distinctive original wShell identity rather than introducing another unrelated symbol. This image will be embedded as the executable ICO; transparent corners must remain transparent.
```
