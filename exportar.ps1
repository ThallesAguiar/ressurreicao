# Exporta o jogo compilado para uma pasta portatil (dist/),
# pronta para copiar para outro PC com Windows 10/11 64 bits.
# Inclui os dados do jogo e a musica/vozes (trilhas do CD).
# Uso: powershell -ExecutionPolicy Bypass -File exportar.ps1

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null

# Executavel + overlays (fases/chefes/armas)
Copy-Item (Join-Path $root "build\pc\sotn.exe") $dist -Force
Copy-Item (Join-Path $root "build\pc\*.dll") $dist -Force

# Dados do jogo (assets extraidos do disco)
Copy-Item (Join-Path $root "disks\us") (Join-Path $dist "disks\us") -Recurse -Force

# Musica e vozes (trilhas do CD, audio XA) - opcional mas recomendado
$t1 = "C:\www\rea\Castlevania - Symphony of the Night (USA)\Castlevania - Symphony of the Night (USA) (Track 1).bin"
$t2 = "C:\www\rea\Castlevania - Symphony of the Night (USA)\Castlevania - Symphony of the Night (USA) (Track 2).bin"
if ((Test-Path $t1) -and (Test-Path $t2)) {
    Copy-Item $t1 (Join-Path $dist "Track 1.bin") -Force
    Copy-Item $t2 (Join-Path $dist "Track 2.bin") -Force
    $cue = @'
FILE "Track 1.bin" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
FILE "Track 2.bin" BINARY
  TRACK 02 AUDIO
    INDEX 00 00:00:00
    INDEX 01 00:02:00
'@
    Set-Content -Path (Join-Path $dist "disc.cue") -Value $cue -Encoding ASCII
    $run = @'
@echo off
cd /d "%~dp0"
sotn.exe --disk "disc.cue"
'@
} else {
    $run = @'
@echo off
cd /d "%~dp0"
sotn.exe
'@
}

Set-Content -Path (Join-Path $dist "run.cmd") -Value $run -Encoding ASCII

Write-Host ""
Write-Host "Pacote pronto em: $dist"
Write-Host "Para usar no outro PC, copie a pasta 'dist' e execute o run.cmd."
Write-Host "Para compactar: Compress-Archive -Path `"$dist\*`" -DestinationPath `"$root\sotn-pc-port.zip`""
