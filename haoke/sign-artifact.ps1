# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory=$true)][string]$Identity,
    [Parameter(Mandatory=$true)][string]$TargetFile,
    [Parameter(Mandatory=$true)][string]$AllowedRoot,
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
# Inno's 32-bit signing host redirects System32 to SysWOW64, which has no WSL.
# Resolve the native system tool explicitly without changing PATH or security.
$wslBinary = if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
    Join-Path $env:WINDIR 'Sysnative\wsl.exe'
} else { Join-Path $env:WINDIR 'System32\wsl.exe' }

function CertificateHash($Certificate) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Certificate.RawData))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function LinuxPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if ($absolute -notmatch '^([A-Za-z]):\\') { throw 'Verification requires a local drive path' }
    return '/mnt/' + $absolute.Substring(0,1).ToLowerInvariant() + $absolute.Substring(2).Replace('\', '/')
}

# Dedicated Inno build callback. Public metadata references an established key;
# no password, PFX export, key creation, elevation or trust-store import occurs.
$metadata = Get-Content -LiteralPath $Identity -Raw -Encoding UTF8 | ConvertFrom-Json
if ($metadata.applicationId -ne 'org.VideoLAN.VLC' -or
    $metadata.certificateSha256 -notmatch '^[a-f0-9]{64}$' -or
    $metadata.certificateThumbprint -notmatch '^[a-fA-F0-9]{40}$') { throw 'Invalid public signing identity metadata' }
$certificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $metadata.certificateThumbprint)
if (-not $certificate.HasPrivateKey -or
    (CertificateHash $certificate) -ne $metadata.certificateSha256 -or
    $certificate.Subject -ne $metadata.publisher -or
    $certificate.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or
    $certificate.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow) { throw 'Established signing identity is unavailable or mismatched' }
if (-not ($certificate.EnhancedKeyUsageList | Where-Object { $_.ObjectId -eq '1.3.6.1.5.5.7.3.3' })) { throw 'Certificate is not a code signing identity' }

$root = Get-Item -LiteralPath $AllowedRoot
$file = Get-Item -LiteralPath $TargetFile
if (-not $root.PSIsContainer -or $file.PSIsContainer -or
    ($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
    ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Signing requires regular task files' }
$base = $root.FullName.TrimEnd('\') + '\'
if (-not $file.FullName.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Signing target is outside the isolated build root' }
$parent = $file.Directory
while ($parent -and $parent.FullName.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
    if ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Signing path contains a reparse point' }
    $parent = $parent.Parent
}
if ($file.Extension -notin @('.exe', '.e32', '.e64', '.dll') -and
    $file.Name -notmatch '^uninst(?:-[a-fA-F0-9]+)?\.e(?:32|64)\.tmp$') { throw 'Unexpected Inno signing target extension' }
$pem = Get-Item -LiteralPath $metadata.publicCertificatePem
if ($pem.PSIsContainer -or ($pem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Public certificate must be a regular file' }
$verificationPem = Join-Path $root.FullName 'haoke-signing-public.pem'
$publicBytes = [IO.File]::ReadAllBytes($pem.FullName)
if (Test-Path -LiteralPath $verificationPem) {
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($verificationPem)) -ne [Convert]::ToBase64String($publicBytes)) { throw 'Verification public certificate differs' }
} else { [IO.File]::WriteAllBytes($verificationPem, $publicBytes) }

$signed = if ($VerifyOnly) { Get-AuthenticodeSignature -LiteralPath $file.FullName }
          else { Set-AuthenticodeSignature -LiteralPath $file.FullName -Certificate $certificate -HashAlgorithm SHA256 }
if (-not $signed.SignerCertificate -or (CertificateHash $signed.SignerCertificate) -ne $metadata.certificateSha256) { throw 'Artifact signer mismatch' }
$priorPreference = $ErrorActionPreference
try {
    # A harmless WSL startup stderr message is separate from verifier exit code.
    $ErrorActionPreference = 'Continue'
    $verification = & $wslBinary -d Ubuntu -- osslsigncode verify -CAfile (LinuxPath $verificationPem) -require-leaf-hash ('sha256:'+$metadata.certificateSha256) -in (LinuxPath $file.FullName) 2>&1
    $verifyExit = $LASTEXITCODE
} finally { $ErrorActionPreference = $priorPreference }
if ($verifyExit -ne 0) {
    $verification | Select-Object -Last 12 | Write-Output
    throw 'Pinned-certificate Authenticode verification failed'
}
if (-not $VerifyOnly -and $file.Name -match '^uninst(?:-[a-fA-F0-9]+)?\.e(?:32|64)\.tmp$') {
    # Inno 6.7 removes this temporary executable after embedding its signed
    # bytes. Keep one public signed binary for the builder's independent check.
    $proofDirectory = Join-Path $root.FullName 'signed-uninstaller'
    if (-not (Test-Path -LiteralPath $proofDirectory -PathType Container)) { throw 'Expected uninstaller proof directory is absent' }
    $proofHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $proofFile = Join-Path $proofDirectory ('uninstaller-' + $proofHash + '.e32')
    if (-not (Test-Path -LiteralPath $proofFile)) { [IO.File]::WriteAllBytes($proofFile, [IO.File]::ReadAllBytes($file.FullName)) }
    if ((Get-FileHash -LiteralPath $proofFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $proofHash) { throw 'Uninstaller proof copy mismatch' }
}
[pscustomobject]@{ file=$file.Name; certificateSha256=$metadata.certificateSha256; windowsTrustStatus=[string]$signed.Status; pinnedCryptographicVerification='passed' } | ConvertTo-Json -Compress
