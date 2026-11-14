$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path (Join-Path $PSScriptRoot "stub.cpp"))) {
    throw "stub.cpp not found"
}
$sourceRoot = "C:\Users\stogi_a87fz74\Desktop\PrismServer\PrismLauncher-develop\PrismLauncher-develop"
$debug = Join-Path $sourceRoot "build\Debug"
$stage = Join-Path $env:TEMP "prismserver-stage"
$zip = Join-Path $env:TEMP "prismserver-payload.zip"
$stub = Join-Path $PSScriptRoot "stub.exe"
$icon = Join-Path $sourceRoot "program_info\prismlauncher.ico"
$out = "C:\Users\stogi_a87fz74\Desktop\PrismServer.exe"

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item $stage -ItemType Directory | Out-Null
foreach ($dir in @("generic", "iconengines", "imageformats", "networkinformation", "platforms", "styles", "tls")) {
    Copy-Item (Join-Path $debug $dir) (Join-Path $stage $dir) -Recurse
}
Get-ChildItem $debug -File | Where-Object {
    $_.Extension -in ".dll", ".exe" -and $_.Name -notlike "LauncherTests*"
} | Copy-Item -Destination $stage
Copy-Item "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT\*" $stage
Set-Content -Path (Join-Path $stage "qt.conf") -Value "[Paths]`r`nPrefix=." -Encoding Ascii

if (Test-Path $zip) { Remove-Item $zip -Force }
& tar.exe -a -c -f $zip -C $stage .
if ($LASTEXITCODE -ne 0) { throw "tar failed" }

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$rc = Join-Path $PSScriptRoot "stub.rc"
$iconRc = ($icon -replace '\\', '/')
Set-Content -Path $rc -Value "1 ICON `"$iconRc`"" -Encoding Ascii
cmd.exe /c "call `"$vcvars`" && cd /d `"$PSScriptRoot`" && rc /nologo stub.rc && cl /nologo /EHsc /std:c++17 /MT /O2 /W3 /DUNICODE /D_UNICODE stub.cpp stub.res /Fe:stub.exe /link /SUBSYSTEM:WINDOWS"
if ($LASTEXITCODE -ne 0) { throw "stub build failed" }

$stamp = [uint64]([DateTime]::UtcNow.Ticks)
$zipBytes = [IO.File]::ReadAllBytes($zip)
$outStream = [IO.File]::Open($out, [IO.FileMode]::Create)
try {
    $stubBytes = [IO.File]::ReadAllBytes($stub)
    $outStream.Write($stubBytes, 0, $stubBytes.Length)
    $outStream.Write($zipBytes, 0, $zipBytes.Length)
    $writer = New-Object IO.BinaryWriter($outStream)
    $writer.Write([uint64]$zipBytes.Length)
    $writer.Write($stamp)
    $writer.Write([Text.Encoding]::ASCII.GetBytes("PRISMPKG"))
    $writer.Flush()
} finally {
    $outStream.Dispose()
}

$mb = [math]::Round((Get-Item $out).Length / 1MB, 1)
Write-Output "Wrote $out ($mb MB)"
