$ErrorActionPreference = "Stop"
$gcc = "G:\Emulacja\redriver2-miyoo\deps\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-linux-gnueabihf\bin\arm-none-linux-gnueabihf-gcc.exe"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $here "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

& $gcc -Wall -Wextra -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard `
    -O2 -fPIC -shared -nostdlib "-Wl,-soname,libdsbench.so" `
    (Join-Path $here "dsbench.c") -lgcc -o (Join-Path $outDir "libdsbench.so")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built: $(Join-Path $outDir 'libdsbench.so')"
