# wShell branding

The current identity is an orange command window containing large lowercase `sh` and an underscore cursor on charcoal. The window outline and title bar make the shell purpose recognizable. The app and connection settings headers use only the text `wShell`, drawn in the embedded JetBrains Mono font. Buttons, selection and focus share the orange accent `#DA702C`.

| Asset | Purpose |
| --- | --- |
| [wshell-icon.png](wshell-icon.png) | Transparent rounded tile for the executable and README |
| [../wshell.ico](../wshell.ico) | Embedded 16, 24, 32, 48, 64, 128 and 256 pixel frames |

The icon was edited with OpenAI's built-in `image_gen` tool on 2026-10-09, using the previous repository icon as the edit target. The terminal window and `sh` replace the `w` glyph. Earlier marks remain in Git history. Assets use the root MIT license; no Termius artwork was used.

PNG-to-ICO conversion preserves transparency and is reproducible with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-icon.ps1
```

## Final edit prompt

```text
Use case: precise-object-edit. Asset: production wShell application icon. Edit the supplied existing icon. Replace the w with a clearly recognizable command terminal window containing exactly lowercase "sh" in large bold monospaced orange lettering. Preserve warm orange #DA702C and charcoal #100F0F identity. A single near-square rounded terminal window fills 88% of the square canvas, with a strong orange outline, a simple orange horizontal title-bar divider near the top and two small orange window control marks. Inside, center the very large lowercase sh, with a small underscore terminal cursor beside it if space permits. sh must dominate and read at small taskbar sizes. Flat solid colors, crisp restrained geometry, no gradients, no texture, no lighting, no shadow, no 3D, no w, no additional words, no watermark. Transparent outside the rounded terminal window. 1024x1024 PNG with real alpha.
```
