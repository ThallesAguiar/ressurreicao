# Generates the browser version separately from the Windows build.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$sdk = Join-Path $root 'build/tools/emsdk'
$toolBin = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $toolBin) {
    $candidate = Get-ChildItem "$env:LOCALAPPDATA/Microsoft/WinGet/Packages/*/mingw64/bin/cmake.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $candidate) { throw 'CMake e Ninja precisam estar instalados.' }
    $env:PATH = "$($candidate.DirectoryName);$env:PATH"
}
$env:PATH = "$env:USERPROFILE/.cargo/bin;$env:PATH"
$emcmake = Join-Path $sdk 'upstream/emscripten/emcmake.exe'
if (-not (Test-Path $emcmake)) {
    throw 'Emscripten ausente. Instale e ative o SDK em build/tools/emsdk conforme docs/WEB.md.'
}
Push-Location $root
try {
    & $emcmake cmake -S . -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl
    if ($LASTEXITCODE -ne 0) { throw 'Falha na configuração web.' }
    cmake --build build/web -j 4
    if ($LASTEXITCODE -ne 0) { throw 'Falha na compilação web.' }
    Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $root 'build/web/site/LICENSE') -Force
    $webFiles = Get-ChildItem (Join-Path $root 'build/web/site') -File |
        Where-Object { $_.Extension -in '.html', '.js', '.wasm', '.data' -or $_.Name -eq 'LICENSE' }
    Compress-Archive -LiteralPath $webFiles.FullName -DestinationPath (Join-Path $root 'build/da-cruz-a-gloria-web.zip') -Force
    Write-Host "Pronto: $root/build/web/site"
    Write-Host "ZIP para publicar: $root/build/da-cruz-a-gloria-web.zip"
    Write-Host 'Teste local: python -m http.server 8080 --directory build/web/site'
} finally {
    Pop-Location
}
