# Build troubleshooting

Check the failed **step**, not just the workflow's red icon. Earlier failed runs remain in Actions after a fix; compare the run's commit with the current branch.

```sh
gh run list --limit 10
gh run view RUN_ID --log-failed
gh run download RUN_ID -n ipad-test-evidence -D build/ipad-evidence-RUN_ID
```

## iPad simulator and device builds

`scripts/build-ios.py` builds both targets. The device IPA deliberately stays unsigned for personal signing. CI needs no Apple account, certificate or provisioning profile.

- OpenSSL architecture flags belong in `CFLAGS`/`LDFLAGS`; passing `arm64` as an extra Configure target fails configuration.
- The pinned SwiftTerm build-info plugin requires a macOS host generator during cross-compilation. The script builds the reviewed generator from the pinned source before Xcode runs the plugin.
- Simulator Keychain error `-34018` means the test app lacks its simulated application identity. Simulator entitlements are embedded in the Mach-O `__TEXT,__entitlements` section. Attaching iOS provisioning entitlements to a macOS ad-hoc signature can instead make the simulator refuse to launch the app.
- The loopback server's `ssh2` API normalizes RSA key types to `ssh-rsa`. Check `hashAlgo` for `sha256`/`sha512` to verify the actual RSA/SHA-2 signature; the key type alone is insufficient.

`ipad-test-evidence` contains app checks and screenshots. On test failure it also preserves server checks and simulator logs before deleting the isolated simulator. A successful compile alone is insufficient: IPA upload requires the SSH, Keychain, terminal and SFTP test step to pass.

## Desktop builds

Windows and macOS run independent jobs. Inspect each job's test evidence before publishing. Release files must come from a successful run, and their SHA-256 values must match the versioned `release/<version>/manifest.json` and uploaded assets.

Physical iPad installation, Korean system-keyboard behavior and personal signing remain separate from simulator validation.
