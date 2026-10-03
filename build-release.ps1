$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

cmake -S . -B build -G "Visual Studio 18 2026" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "AudioPass: $PSScriptRoot\build\bin\Release\AudioPass.exe"
