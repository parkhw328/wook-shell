# wShell branding

The current identity is an orange command window containing a large `>_` terminal prompt on charcoal. The window outline and title bar make the shell purpose recognizable. The app and connection settings headers use only the text `wShell`, drawn in the embedded JetBrains Mono font. Buttons, selection and focus share the orange accent `#DA702C`.

| Asset | Purpose |
| --- | --- |
| [wshell-icon.png](wshell-icon.png) | Transparent rounded tile for the executable and README |
| [../wshell.ico](../wshell.ico) | Embedded 16, 24, 32, 48, 64, 128 and 256 pixel frames |

The icon was edited with OpenAI's built-in `image_gen` tool on 2026-10-09, using the previous repository icon as the edit target. The terminal window and `>_` prompt replace the previous letter mark. Earlier marks remain in Git history. Assets use the root MIT license; no Termius artwork was used.

PNG-to-ICO conversion preserves transparency and is reproducible with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-icon.ps1
```

## Final edit prompt

```text
Use case: precise-object-edit. Edit target: supplied current wShell icon. Keep the existing single rounded command window, orange outline, title bar with two dots, charcoal interior, transparent exterior and centered layout. Replace all of the large lowercase sh and its cursor with ONLY a large bold terminal prompt symbol ">_" inside the window. The > chevron and underscore must together be clearly centered, balanced, thick and immediately legible at small Windows taskbar sizes. No letters at all. Preserve warm orange #DA702C and charcoal #100F0F branding. Simple flat solid colors, crisp edges, no texture, no gradients, no shadows, no 3D, no extra words or decorations. Transparent PNG with real alpha outside the rounded window.

Final repair: Repair the >_ icon: preserve geometry, make the entire window interior opaque flat charcoal #100F0F, orange surfaces #DA702C, and retain transparency only outside the rounded silhouette; remove exterior stray pixels.
```
