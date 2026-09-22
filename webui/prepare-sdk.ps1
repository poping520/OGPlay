$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdkRoot = Join-Path $repoRoot '.local/gui-v2'
New-Item -ItemType Directory -Force $sdkRoot | Out-Null
$archive = Join-Path $sdkRoot 'webview2.zip'
Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.1150.38/microsoft.web.webview2.1.0.1150.38.nupkg' -OutFile $archive -TimeoutSec 120
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '921C004BD1764B585496B2EB3EEC0A59A9A98E698246F1D9A3F1C08D1D84EBD5') {
    throw 'WebView2 SDK SHA-256 mismatch'
}
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $sdkRoot 'webview2') -Force
