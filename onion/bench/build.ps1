$ErrorActionPreference = "Stop"
$gcc = if ($env:ARM_GCC) { $env:ARM_GCC } else { "arm-none-linux-gnueabihf-gcc" }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $here "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

& $gcc -Wall -Wextra -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard `
    -O2 -fPIC -shared -nostdlib "-Wl,-soname,libdsbench.so" `
    (Join-Path $here "dsbench.c") -lgcc -o (Join-Path $outDir "libdsbench.so")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built: $(Join-Path $outDir 'libdsbench.so')"
