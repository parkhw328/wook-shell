# Third-party notices

wShell's original code and original branding assets are MIT licensed; see LICENSE.
Third-party components keep their own copyright and license terms.
wShell is not affiliated with or endorsed by PuTTY, Termius, JetBrains, or Steph Ango.

| Component | Version/source | License | Changes |
| --- | --- | --- | --- |
| PuTTY | [0.85 official source](https://www.chiark.greenend.org.uk/~sgtatham/putty/releases/0.85.html) | MIT, licenses/PuTTY-MIT.txt | Single-executable entry point, file storage, embedded fonts, tab hosting, keyboard routing, local preview, direct key-management API; cryptographic algorithms unchanged |
| Flexoki | [kepano/flexoki](https://github.com/kepano/flexoki), commit 8d723bac4a9ac46adfdf99d42155286977aac72a | MIT, licenses/Flexoki-MIT.txt | Palette used in native UI and terminal defaults |
| JetBrains Mono | [v2.304](https://github.com/JetBrains/JetBrainsMono/tree/v2.304) | SIL OFL 1.1, licenses/JetBrainsMono-OFL.txt | Unmodified Regular and Bold TTF files |
| LLVM libc++ / libc++abi / compiler-rt / libunwind | [LLVM MinGW 20260922](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922), LLVM 23.1.2 | Apache 2.0 with LLVM exceptions and accompanying legacy notices, licenses/LLVM-*.txt | Statically linked runtime code |
| MinGW-w64 runtime | Included with LLVM MinGW 20260922 | Permissive licenses, licenses/MinGW-w64-runtime.txt | Statically linked Windows runtime support |
| winpthreads | Included with LLVM MinGW 20260922 | MIT/BSD notices, licenses/winpthreads.txt | Runtime support bundled by the static toolchain |

PuTTY source SHA-256: 232c5c286a5b35f445dbbf49e159469acde372a3907aef738d88e28b4b0f6da2.
Font SHA-256: Regular a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f;
Bold 5590990c82e097397517f275f430af4546e1c45cff408bde4255dad142479dcb.

The terminal is **modified PuTTY source statically linked into wShell.exe**, not an official PuTTY binary.
The native key manager uses PuTTY's Ed25519/key-format routines and Windows CNG for RSA generation.
PuTTY's guard against linking unsafe RSA-generation primitives into an SSH client remains intact.
Its source is reproduced by scripts/bootstrap.py and scripts/patch-putty.py.
The complete applicable license texts are in the source licenses directory and embedded in the distributed wShell.exe.
Open Tools → Open-source licenses in the application to read those notices; no adjacent license files are required.

The `ssh2` package and its transitive dependencies are development-only fixtures under tests/ssh;
they are not included in the portable ZIP. Their package manifests retain their own licenses.
The AI-generated wShell branding assets and exact generation prompts are documented in assets/branding/README.md.
