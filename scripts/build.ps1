param([switch]$Bootstrap, [switch]$EngineOnly, [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    if ($Bootstrap) { python scripts/bootstrap.py; if ($LASTEXITCODE) { throw 'Bootstrap failed' } }
    $llvmBin = (Join-Path $repoRoot '.tools/llvm-mingw-20260922-ucrt-x86_64/bin').Replace('\', '/')
    $cmake = Join-Path $repoRoot '.tools/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
    $ctest = Join-Path $repoRoot '.tools/cmake-4.4.4-windows-x86_64/bin/ctest.exe'
    $ninja = (Join-Path $repoRoot '.tools/ninja/ninja.exe').Replace('\', '/')
    if (!(Test-Path -LiteralPath $cmake)) { throw 'Run python scripts/bootstrap.py first.' }
    $env:PATH = "$llvmBin;$env:PATH"
    $commonArgs = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_C_COMPILER=$llvmBin/x86_64-w64-mingw32-gcc.exe", "-DCMAKE_RC_COMPILER=$llvmBin/llvm-windres.exe", '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXE_LINKER_FLAGS=-static -Wl,--nxcompat,--dynamicbase,--high-entropy-va')
    python scripts/patch-putty.py
    if ($LASTEXITCODE) { throw 'PuTTY integration patch failed' }
    python scripts/embed-notices.py
    if ($LASTEXITCODE) { throw 'License resource generation failed' }
    & $cmake -S .deps/putty -B build/native @commonArgs "-DCMAKE_CXX_COMPILER=$llvmBin/x86_64-w64-mingw32-g++.exe" "-DWSHELL_ROOT=$($repoRoot.Replace('\', '/'))"
    if ($LASTEXITCODE) { throw 'Engine configure failed' }
    & $cmake --build build/native --target WookShell ui-smoke-tests core-tests ime-tests sftp-tests sftp-codec-tests launch-relay-tests plink -j 8
    if ($LASTEXITCODE) { throw 'Engine build failed' }
    if (!$EngineOnly) {
        if (!$SkipTests) {
            & $ctest --test-dir build/native/workspace --output-on-failure
            if ($LASTEXITCODE) { throw 'Tests failed' }
        }
        python scripts/package.py
        if ($LASTEXITCODE) { throw 'Packaging failed' }
        python scripts/verify-package.py
        if ($LASTEXITCODE) { throw 'Portable package verification failed' }
        python scripts/index-dist.py
        if ($LASTEXITCODE) { throw 'Release manifest verification failed' }
    }
} finally { Pop-Location }
