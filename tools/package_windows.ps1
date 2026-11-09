# Windows x64 portable release. Run from any directory with CMake on PATH.
[CmdletBinding()]
param(
    [string]$CMake = 'cmake.exe',
    [string]$OutputDirectory = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root 'out/releases' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$versionMatch = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw -Encoding UTF8),
    'project\(OGPlay VERSION ([0-9.]+)')
if (!$versionMatch.Success) { throw 'Cannot read project version' }
$name = 'OGPlay-v' + $versionMatch.Groups[1].Value + '-windows-x64'
$stage = Join-Path $OutputDirectory $name
$archive = "$stage.zip"
if ((Test-Path $stage) -or (Test-Path $archive)) { throw "Output already exists: $stage" }

Push-Location $root
try {
    if (!$SkipBuild) {
        & $CMake --preset windows-msvc -DOGPLAY_BUILD_GUI=ON -DOGPLAY_BUILD_WEBUI=ON
        if ($LASTEXITCODE) { throw 'CMake configure failed' }
        & $CMake --build --preset windows-msvc --config Release --target ogplay ogplay-cli
        if ($LASTEXITCODE) { throw 'Release build failed' }
    }
    & $CMake --install build/windows-msvc --config Release --component Runtime --prefix $stage
    if ($LASTEXITCODE) { throw 'Runtime install failed' }
    # Keep the executable and its data together at the root of the ZIP.
    Get-ChildItem (Join-Path $stage 'bin') | Move-Item -Destination $stage
    Remove-Item (Join-Path $stage 'bin')

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    # Match the toolchain used by this build rather than an unrelated VS installation.
    $cache = Get-Content 'build/windows-msvc/CMakeCache.txt' -Raw -Encoding UTF8
    $instance = [regex]::Match($cache, '(?m)^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)\r?$')
    if ($instance.Success) { $vs = $instance.Groups[1].Value.Trim() }
    else { $vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) }
    if (!$vs) { throw 'Visual Studio C++ tools not found' }
    $redist = Get-ChildItem (Join-Path $vs 'VC/Redist/MSVC') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$redist) { throw 'MSVC redistributable directory not found' }
    $crt = @(Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT')
    if ($crt.Count -ne 1) { throw 'Expected one x64 release CRT directory' }
    Copy-Item (Join-Path $crt[0].FullName '*.dll') $stage

    Copy-Item 'LICENSE' $stage
    # Native/Web UI license texts already live in credits.json. This SDK license
    # is platform-specific and is not included in that catalog.
    Copy-Item 'third_party/angle-prebuilt/licenses/Microsoft-Windows-SDK.rtf' `
        (Join-Path $stage 'WINDOWS-SDK-LICENSE.rtf')
    $credits = Get-Content (Join-Path $stage 'data/credits.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($project in $credits.projects) {
        foreach ($notice in $project.notices) {
            if ($notice.PSObject.Properties['path']) {
                if (!(Test-Path (Join-Path $stage ('data/' + $notice.path)))) {
                    throw "Missing notice: $($notice.path)"
                }
            }
        }
    }
    [IO.File]::WriteAllText((Join-Path $stage 'README.txt'), @'
OGPlay - Windows x64 portable release

Extract the entire ZIP to a writable folder, then run ogplay.exe.
Keep data/ and all DLLs beside the executable. No Node.js or Visual Studio installation is required.
This package includes the x64 Visual C++ Release runtime DLLs.

The launcher requires Microsoft Edge WebView2 Evergreen Runtime.
If WebView2 cannot initialize, OGPlay offers to open the official Microsoft download page.
Install the Evergreen Runtime and restart OGPlay:
https://developer.microsoft.com/microsoft-edge/webview2/

Command line: ogplay-cli.exe --help
Import your own APK and any required game data using the launcher.
Only OGPlay's own demo game is included; commercial games are not included.
User settings, library and saves are stored outside this program folder.

This is an early release. Compatibility depends on the game and APK version.
Licenses: LICENSE, data/credits.json (also shown in About),
data/android/19/notices/, WINDOWS-SDK-LICENSE.rtf,
data/webui/gui/WEBVIEW2-SDK-LICENSE.txt.
FFmpeg source and build instructions: https://github.com/poping520/OGPlay-ffmpeg-prebuilt
Visual C++ runtime redistribution: https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
'@, [Text.UTF8Encoding]::new($false))

    # Audit every packaged native module's imports, including delay-load DLLs.
    $tools = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $dumpbin = Join-Path $tools.FullName 'bin/Hostx64/x64/dumpbin.exe'
    $modules = @(Get-ChildItem $stage -File | Where-Object { $_.Extension -in '.exe', '.dll' })
    foreach ($module in $modules) {
        $headers = & $dumpbin /headers $module.FullName
        if ($LASTEXITCODE -or !(($headers -join "`n") -match '8664 machine')) {
            throw "Not a Windows x64 module: $($module.Name)"
        }
        $imports = & $dumpbin /dependents $module.FullName
        if ($LASTEXITCODE) { throw "Cannot inspect $($module.Name)" }
        foreach ($line in $imports) {
            if ($line -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') {
                $dll = $Matches[1]
                if ($dll -match '^(api-ms-|ext-ms-)') { continue }
                if (Test-Path (Join-Path $stage $dll)) { continue }
                # MSVC runtimes must be app-local even if this developer machine has them installed.
                if ($dll -match '^(msvcp\d|vcruntime\d|concrt\d|vcomp\d)' -or
                    !(Test-Path (Join-Path "$env:WINDIR/System32" $dll))) {
                    throw "Missing packaged dependency: $($module.Name) -> $dll"
                }
            }
        }
    }
    foreach ($required in 'ogplay.exe', 'ogplay-cli.exe', 'ogplaycore.dll', 'data/profiles', 'data/webui/gui/manifest.json',
        'data/android/19/framework/ogplay.jar', 'data/android/19/framework/framework.jar', 'data/credits.json') {
        if (!(Test-Path (Join-Path $stage $required))) { throw "Missing runtime file: $required" }
    }
    $files = @(Get-ChildItem $stage -File -Recurse | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($stage.Length + 1).Replace('\', '/');
            bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    [ordered]@{ schema = 1; version = $versionMatch.Groups[1].Value; platform = 'windows-x64';
        crt = $redist.Name; files = $files } | ConvertTo-Json -Depth 5 |
        Set-Content (Join-Path $stage 'package-manifest.json') -Encoding UTF8
    # PowerShell 5 Compress-Archive emits backslashes in ZIP entry names. Use
    # standard '/' separators so all extractors preserve the directory tree.
    Add-Type -AssemblyName System.IO.Compression
    $zip = [IO.Compression.ZipArchive]::new([IO.File]::Create($archive), [IO.Compression.ZipArchiveMode]::Create)
    try {
        # profiles/ may legitimately be empty; payload discovery still requires it.
        foreach ($directory in Get-ChildItem $stage -Directory -Recurse -Force) {
            $relative = $name + '/' + $directory.FullName.Substring($stage.Length + 1).Replace('\', '/') + '/'
            $null = $zip.CreateEntry($relative)
        }
        foreach ($file in Get-ChildItem $stage -File -Recurse -Force) {
            $relative = $name + '/' + $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
            $entry = $zip.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $inputStream = [IO.File]::OpenRead($file.FullName)
            try {
                $outputStream = $entry.Open()
                try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
            } finally { $inputStream.Dispose() }
        }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $OutputDirectory ($name + '.sha256')),
        "$hash  $name.zip`n", [Text.UTF8Encoding]::new($false))
    Write-Output "Package: $archive"
    Write-Output "SHA256: $hash"
} finally { Pop-Location }
