# wShell branding

The current identity is a simple orange lowercase `w` on a charcoal tile. The app and connection settings headers use only the text `wShell`, drawn in the embedded JetBrains Mono font. Buttons, selection and focus share the orange accent `#DA702C`.

| Asset | Purpose |
| --- | --- |
| [wshell-icon.png](wshell-icon.png) | Transparent rounded tile for the executable and README |
| [../wshell.ico](../wshell.ico) | Embedded 16, 24, 32, 48, 64, 128 and 256 pixel frames |

The icon was edited with OpenAI's built-in `image_gen` tool on 2026-10-07, using the previous repository icon as the edit target. The orange glyph replaces the mint ribbon, gray segments, chevron and cursor. The previous mark and wordmark remain in Git history. Assets use the root MIT license; no Termius artwork was used.

PNG-to-ICO conversion preserves transparency and is reproducible with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-icon.ps1
```

## Final edit prompt

```text
Use case: precise-object-edit. Asset type: production Windows application icon for wShell. Image 1 is the edit target. Keep the square format, generous safe padding, and rounded charcoal tile with transparent outside corners. Replace the entire teal ribbon/chevron/cursor emblem with ONE simple, clearly readable lowercase "w", centered, in a single solid warm orange #DA702C. Use a bold, clean, contemporary rounded geometric glyph with balanced negative space that stays readable at 16 pixels. The w must be one continuous uniform orange shape: remove every gray, white, parchment or folded ribbon segment, remove the arrow/chevron and cursor, and remove all gradients, grain, texture, lighting, depth, highlights, shadows and bevels. Tile interior should be flat Flexoki charcoal #100F0F with a thin subtle dark #282726 outline. High-quality crisp edges and restrained geometry. No teal or mint anywhere. No other letters, no wordmark, no badges, no mockup, no watermark. 1024 x 1024 transparent PNG. Preserve real alpha outside the rounded square.
```
