param(
    [Parameter(Mandatory = $true)][string]$Thumbprint,
    [Parameter(Mandatory = $true)][uri]$TimestampUrl,
    [string]$SignTool = 'signtool.exe',
    [string]$Binary = (Join-Path $PSScriptRoot '../build/native/wShell.exe'),
    [switch]$LocalMachine
)
$ErrorActionPreference = 'Stop'
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$tool = (Get-Command $SignTool -ErrorAction Stop).Source
$thumbprintValue = $Thumbprint.Replace(' ', '')
if ($thumbprintValue -notmatch '^[0-9a-fA-F]{40}$') { throw 'Expected a certificate SHA-1 thumbprint.' }
if ($TimestampUrl.Scheme -notin @('http', 'https')) { throw 'Use the certificate provider RFC 3161 HTTP(S) timestamp URL.' }
$store = if ($LocalMachine) { 'LocalMachine' } else { 'CurrentUser' }
$certificate = Get-Item -LiteralPath "Cert:\$store\My\$thumbprintValue"
if (!$certificate.HasPrivateKey) { throw 'The signing certificate has no accessible private key.' }
if ($certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date)) { throw 'The signing certificate is not currently valid.' }
if (@($certificate.EnhancedKeyUsageList | Where-Object ObjectId -eq '1.3.6.1.5.5.7.3.3').Count -eq 0) { throw 'A code-signing certificate is required.' }
$arguments = @('sign', '/sha1', $thumbprintValue, '/s', 'My', '/fd', 'SHA256', '/tr', $TimestampUrl.AbsoluteUri, '/td', 'SHA256', '/d', 'wShell')
if ($LocalMachine) { $arguments += '/sm' }
& $tool @arguments $binaryPath
if ($LASTEXITCODE) { throw 'Authenticode signing failed.' }
& $tool verify /pa /all /v $binaryPath
if ($LASTEXITCODE) { throw 'Authenticode verification failed.' }
$signature = Get-AuthenticodeSignature -LiteralPath $binaryPath
if ($signature.Status -ne 'Valid' -or !$signature.TimeStamperCertificate) { throw 'A valid timestamped signature is required.' }
if ($signature.SignerCertificate.Thumbprint -ne $thumbprintValue) { throw 'Unexpected signing certificate.' }
Write-Output "Verified timestamped Authenticode signature: $($signature.SignerCertificate.Subject)"
