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
    & $cmake -S .deps/putty -B build/engine @commonArgs
    if ($LASTEXITCODE) { throw 'Engine configure failed' }
    & $cmake --build build/engine --target putty puttygen pageant plink pscp psftp -j 8
    if ($LASTEXITCODE) { throw 'Engine build failed' }
    if (!$EngineOnly) {
        & $cmake -S . -B build/app @commonArgs "-DCMAKE_CXX_COMPILER=$llvmBin/x86_64-w64-mingw32-g++.exe"
        if ($LASTEXITCODE) { throw 'App configure failed' }
        & $cmake --build build/app -j 8
        if ($LASTEXITCODE) { throw 'App build failed' }
        if (!$SkipTests) {
            & $ctest --test-dir build/app --output-on-failure
            if ($LASTEXITCODE) { throw 'Tests failed' }
        }
        python scripts/package.py
        if ($LASTEXITCODE) { throw 'Packaging failed' }
    }
} finally { Pop-Location }
