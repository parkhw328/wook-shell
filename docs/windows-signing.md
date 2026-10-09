# Windows release signing

Public EXE signing requires a publicly trusted code-signing certificate or signing service and access to its private-key operation. No signing identity is currently configured. Do not substitute a self-signed certificate for a public release identity.

The certificate holder completes identity validation and any purchase directly with the provider. Keep private keys, token PINs and service credentials outside this repository and chat.

For a provider that exposes its certificate in the Windows Personal certificate store, install its key/token middleware and the Windows SDK SignTool, then:

```powershell
# Compile without publishing an unsigned release.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -EngineOnly
.tools/cmake-4.4.4-windows-x86_64/bin/ctest.exe --test-dir build/native/workspace --output-on-failure

# Use the provider's RFC 3161 timestamp URL and your certificate thumbprint.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/sign-windows.ps1 -Thumbprint '<certificate-thumbprint>' -TimestampUrl '<provider-timestamp-url>' -SignTool '<path-to-signtool.exe>'

# Package only after signing: ZIP and checksums must cover the signed EXE.
python scripts/package.py
python scripts/verify-package.py
python scripts/index-dist.py
```

Cloud services may require a provider-specific signing client instead. Verify the final EXE with SignTool and `Get-AuthenticodeSignature`, including its publisher and timestamp, before packaging. Rebuilding or changing the EXE after signing requires signing it again. Never replace an already published version's artifacts.

Signing identifies the publisher and detects changes; it does not guarantee immediate SmartScreen reputation or a clean security audit.

References: [Microsoft SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool), [Authenticode timestamping](https://learn.microsoft.com/en-us/windows/win32/seccrypto/time-stamping-authenticode-signatures).
