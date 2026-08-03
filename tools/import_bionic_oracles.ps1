[CmdletBinding()]
param(
    [string]$SourceRoot,
    [string]$DestinationRoot = (Join-Path $PSScriptRoot '..\.local\bionic-oracle'),
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$ApiLevels = @(19, 22, 23)
$CommonFiles = @(
    'bin/linker',
    'lib/libc.so',
    'lib/libdl.so',
    'lib/liblog.so',
    'lib/libm.so',
    'lib/libstdc++.so',
    'lib/libz.so'
)
$SixtyFourBitFiles = @(
    'bin/linker64',
    'lib64/libc.so',
    'lib64/libdl.so',
    'lib64/liblog.so',
    'lib64/libm.so',
    'lib64/libstdc++.so',
    'lib64/libz.so'
)

function Get-ExpectedFiles([int]$ApiLevel) {
    if ($ApiLevel -eq 19) { return $CommonFiles }
    return $CommonFiles + $SixtyFourBitFiles
}

function Get-ElfIdentity([string]$Path, [bool]$Expect64Bit) {
    $header = [byte[]]::new(20)
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        if ($stream.Read($header, 0, $header.Length) -ne $header.Length) {
            throw "ELF header is truncated"
        }
    } finally {
        $stream.Dispose()
    }
    if ($header[0] -ne 0x7f -or $header[1] -ne 0x45 -or
        $header[2] -ne 0x4c -or $header[3] -ne 0x46) {
        throw "file is not ELF"
    }
    if ($header[5] -ne 1) { throw "only little-endian ELF is supported" }
    $expectedClass = if ($Expect64Bit) { 2 } else { 1 }
    $expectedMachine = if ($Expect64Bit) { 183 } else { 40 }
    $machine = [int]$header[18] -bor ([int]$header[19] -shl 8)
    if ($header[4] -ne $expectedClass -or $machine -ne $expectedMachine) {
        throw "ELF class or machine does not match its directory"
    }
    return [ordered]@{ elf_class = $expectedClass; machine = $expectedMachine }
}

function Get-OracleEntries([string]$Root) {
    $entries = @()
    foreach ($api in $ApiLevels) {
        $apiRoot = Join-Path $Root "api$api"
        if (-not (Test-Path -LiteralPath $apiRoot -PathType Container)) {
            throw "missing API directory: api$api"
        }
        $expected = @(Get-ExpectedFiles $api)
        $actual = @(Get-ChildItem -LiteralPath $apiRoot -Recurse -File | ForEach-Object {
            $_.FullName.Substring($apiRoot.Length + 1).Replace('\', '/')
        } | Sort-Object)
        $difference = @(Compare-Object ($expected | Sort-Object) $actual)
        if ($difference.Count -ne 0) { throw "unexpected file set for API $api" }

        foreach ($relative in $expected) {
            $path = Join-Path $apiRoot $relative
            $is64Bit = $relative -eq 'bin/linker64' -or $relative.StartsWith('lib64/')
            $identity = Get-ElfIdentity $path $is64Bit
            $item = Get-Item -LiteralPath $path
            $entries += [ordered]@{
                api = $api
                path = $relative
                size = $item.Length
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
                elf_class = $identity.elf_class
                machine = $identity.machine
            }
        }
    }
    return $entries
}

$destination = [System.IO.Path]::GetFullPath($DestinationRoot)
if (-not $ValidateOnly) {
    if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
        throw 'SourceRoot is required unless ValidateOnly is set'
    }
    $source = [System.IO.Path]::GetFullPath($SourceRoot)
    foreach ($api in $ApiLevels) {
        foreach ($relative in (Get-ExpectedFiles $api)) {
            $sourcePath = Join-Path (Join-Path $source "api$api") $relative
            if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
                throw "source is missing an expected API $api file"
            }
            $destinationPath = Join-Path (Join-Path $destination "api$api") $relative
            New-Item -ItemType Directory -Force -Path (Split-Path $destinationPath) | Out-Null
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        }
    }
}

$entries = @(Get-OracleEntries $destination)
$manifest = [ordered]@{
    schema_version = 1
    api_levels = $ApiLevels
    files = $entries
}
$manifestPath = Join-Path $destination 'manifest.json'
if ($ValidateOnly) {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'oracle manifest is missing'
    }
    $stored = Get-Content -LiteralPath $manifestPath -Encoding UTF8 -Raw | ConvertFrom-Json
    $actualJson = $manifest | ConvertTo-Json -Depth 5 -Compress
    $storedJson = $stored | ConvertTo-Json -Depth 5 -Compress
    if ($actualJson -cne $storedJson) { throw 'oracle manifest does not match files' }
} else {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    ($manifest | ConvertTo-Json -Depth 5) + [Environment]::NewLine |
        Set-Content -LiteralPath $manifestPath -Encoding UTF8 -NoNewline
}

Write-Output "Bionic oracle validated: 3 API levels, $($entries.Count) files"
