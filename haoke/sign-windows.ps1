# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory=$true)][string]$Identity,
    [Parameter(Mandatory=$true)][string]$RuntimeRoot
)
$ErrorActionPreference = 'Stop'
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

# This script reuses an approved identity. It never generates keys, exports a
# private key, imports a trust root, changes policy, or elevates privileges.
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
$publicPem = Get-Item -LiteralPath $metadata.publicCertificatePem
if ($publicPem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Public certificate must be a regular local file' }
$runtime = Get-Item -LiteralPath $RuntimeRoot
if (-not $runtime.PSIsContainer -or ($runtime.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Runtime must be a regular task directory' }
$base = $runtime.FullName.TrimEnd('\') + '\'
$verificationPem=Join-Path $runtime.Parent.FullName 'haoke-signing-public.pem'
$publicBytes=[IO.File]::ReadAllBytes($publicPem.FullName)
if(Test-Path -LiteralPath $verificationPem) {
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($verificationPem)) -ne [Convert]::ToBase64String($publicBytes)) {throw 'Existing verification public certificate differs'}
} else { [IO.File]::WriteAllBytes($verificationPem,$publicBytes) }
$results = @()
foreach ($relative in @('vlc.exe', 'plugins\gui\libqt_plugin.dll')) {
    $file = Get-Item -LiteralPath (Join-Path $base $relative)
    if (-not $file.FullName.StartsWith($base, [StringComparison]::OrdinalIgnoreCase) -or
        ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected signing target' }
    $signed = Set-AuthenticodeSignature -LiteralPath $file.FullName -Certificate $certificate -HashAlgorithm SHA256
    if (-not $signed.SignerCertificate -or (CertificateHash $signed.SignerCertificate) -ne $metadata.certificateSha256) { throw 'Artifact signer mismatch' }
    # Trust the pinned public certificate for this verification only. This is
    # independent cryptographic/digest verification, not a Windows trust-store edit.
    $priorPreference=$ErrorActionPreference
    try {
        # Windows PowerShell treats a harmless WSL startup stderr warning as a
        # terminating NativeCommandError. The verifier's exit status remains
        # mandatory; stderr alone is not a cryptographic verification result.
        $ErrorActionPreference='Continue'
        $verification = & $wslBinary -d Ubuntu -- osslsigncode verify -CAfile (LinuxPath $verificationPem) -require-leaf-hash ('sha256:'+$metadata.certificateSha256) -in (LinuxPath $file.FullName) 2>&1
        $verifyExit=$LASTEXITCODE
    } finally { $ErrorActionPreference=$priorPreference }
    if ($verifyExit -ne 0) {
        $verification | Select-Object -Last 12 | Write-Output
        throw 'Pinned-certificate Authenticode verification failed'
    }
    $results += [pscustomobject]@{ file=$relative; certificateSha256=$metadata.certificateSha256; windowsTrustStatus=[string]$signed.Status; pinnedCryptographicVerification='passed' }
}
$results | ConvertTo-Json -Compress
