# Third-party notices

Wook Shell's original code is MIT licensed; see LICENSE.
Third-party components keep their own copyright and license terms.
Wook Shell is not affiliated with or endorsed by PuTTY, Termius, JetBrains, or Steph Ango.

| Component | Version/source | License | Changes |
| --- | --- | --- | --- |
| PuTTY | [0.85 official source](https://www.chiark.greenend.org.uk/~sgtatham/putty/releases/0.85.html) | MIT, licenses/PuTTY-MIT.txt | File storage, private font loading, tab hosting, keyboard routing, local preview; no crypto changes |
| Flexoki | [kepano/flexoki](https://github.com/kepano/flexoki), commit 8d723bac4a9ac46adfdf99d42155286977aac72a | MIT, licenses/Flexoki-MIT.txt | Palette used in native UI and terminal defaults |
| JetBrains Mono | [v2.304](https://github.com/JetBrains/JetBrainsMono/tree/v2.304) | SIL OFL 1.1, licenses/JetBrainsMono-OFL.txt | Unmodified Regular and Bold TTF files |

PuTTY source SHA-256: 232c5c286a5b35f445dbbf49e159469acde372a3907aef738d88e28b4b0f6da2.
Font SHA-256: Regular a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f;
Bold 5590990c82e097397517f275f430af4546e1c45cff408bde4255dad142479dcb.

The portable engine is a **modified PuTTY build**, named wook-putty.exe.
Its source is reproduced by scripts/bootstrap.py and scripts/patch-putty.py.
The complete applicable license texts are included in the licenses directory in both source and ZIP distributions.
