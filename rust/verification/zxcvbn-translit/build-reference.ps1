$ErrorActionPreference = 'Stop'

$scriptDirectory = $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $scriptDirectory '..\..\..')).Path
$referenceDirectory = Join-Path $scriptDirectory 'reference'
New-Item -ItemType Directory -Force -Path $referenceDirectory | Out-Null

$sourceFiles = @(
    (Join-Path $repositoryRoot 'Telegram/SourceFiles/_other/zxcvbn_translit.cpp'),
    (Join-Path $repositoryRoot 'Telegram/SourceFiles/ui/passcode_strength_translit.cpp')
)

& g++ -std=c++20 -O2 "-I$(Join-Path $repositoryRoot 'Telegram/SourceFiles')" @sourceFiles -o (Join-Path $referenceDirectory 'zxcvbn_translit.exe')
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build the original C++ zxcvbn_translit reference (exit $LASTEXITCODE)."
}

Write-Output (Join-Path $referenceDirectory 'zxcvbn_translit.exe')
