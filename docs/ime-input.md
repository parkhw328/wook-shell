# IME input verification

## Automated checks

- `scripts/build.ps1` runs `ime-tests`: successive `ㅎ → 하 → 한` previews, editing/cancellation, simultaneous commit and next composition, surrogate pairs, two-cell Hangul, resized bounds, and cleanup. These are synthetic IMM snapshots passed to the production component.
- `node tests/ssh/integration.cjs` sends Unicode `WM_IME_CHAR` messages through an embedded terminal and checks that `한글🙂` reaches the independent SSH server exactly once. `ui-ime-composition.bmp` captures a synthetic preview at the hosted terminal's caret.
- `node tests/ssh/mac-integration.cjs` exercises AppKit marked text, selected ranges, attributed commits, cancellation, and exact SSH input. `mac-ime-composition.png` captures the marked text.

## Physical input checks

Use a disposable local shell or test SSH session. Do not run incomplete commands against production hosts.

1. Select the OS Korean input method. Type `한글 입력 테스트` slowly. The composing syllable should stay at the terminal caret with an orange underline; a second composition popup should not appear.
2. During a syllable, press Backspace to edit the composition, then finish it with Space. Verify that no extra jamo or duplicate syllables reach the shell.
3. Start a composition and switch tabs. On Windows the confirmed result belongs to the original tab; the new tab must not inherit its draft.
4. Resize the window and repeat near the right/bottom edges. Repeat at 100%, 150%, and 200% display scaling, including moving between monitors.
5. With a supported Korean input method, open the Hanja candidate list. Verify that it stays beside the composition and that selection still works.
6. Repeat in CMD, PowerShell, an SSH shell, and the intended terminal editor/CLI. On macOS also check Korean input with an attributed commit and ordinary arrow/Backspace keys after committing.

Record OS version, input method/version, display scaling, and the remote program when reporting a mismatch. Automated checks do not emulate a physical keyboard or certify every OS IME.

## API references

- [Microsoft: WM_IME_COMPOSITION](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-composition)
- [Microsoft: WM_IME_SETCONTEXT](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-setcontext)
- [Microsoft: Unicode WM_IME_CHAR](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-char)
- [Apple: marked text and relative selection](https://developer.apple.com/documentation/appkit/nstextinputclient/setmarkedtext(_:selectedrange:replacementrange:))
