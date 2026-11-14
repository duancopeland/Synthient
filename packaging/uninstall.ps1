<#
.SYNOPSIS
  Removes a per-user Synthient install.

.DESCRIPTION
  Deletes the install folder (including analysis\.venv and plug-ins placed in
  instruments\), the Start Menu and Desktop shortcuts, and the HKCU uninstall
  entry. User data in %APPDATA%\Synthient (songs, Claude memory, settings) is
  kept unless -RemoveUserData is given. ANTHROPIC_API_KEY is kept unless
  -RemoveApiKey is given.

.PARAMETER InstallDir
  Folder to remove. Default: the folder this script is in, if it holds
  Synthient.exe; otherwise the InstallLocation from the registry; otherwise
  %LOCALAPPDATA%\Programs\Synthient.
#>
[CmdletBinding()]
param(
    [string]$InstallDir,
    [switch]$RemoveUserData,
    [switch]$RemoveApiKey,
    [switch]$NoShortcuts,
    [switch]$NoRegistry,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$UninstKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Synthient'
function Say([string]$Msg) { if (-not $Quiet) { Write-Host $Msg } }

if (-not $InstallDir) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'Synthient.exe')) { $InstallDir = $PSScriptRoot }
    else {
        try { $InstallDir = (Get-ItemProperty -LiteralPath $UninstKey -Name InstallLocation -ErrorAction Stop).InstallLocation } catch { }
        if (-not $InstallDir) { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\Synthient' }
    }
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$exe = Join-Path $InstallDir 'Synthient.exe'

# Running?
$running = Get-Process -Name Synthient -ErrorAction SilentlyContinue |
    Where-Object { try { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -ieq $exe) } catch { $false } }
if ($running) { throw "Synthient is running from $InstallDir. Close it first." }

# ---- shortcuts --------------------------------------------------------------------
if (-not $NoShortcuts) {
    $shell = New-Object -ComObject WScript.Shell
    foreach ($lnkPath in @(
            (Join-Path ([Environment]::GetFolderPath('Programs')) 'Synthient.lnk'),
            (Join-Path ([Environment]::GetFolderPath('Desktop'))  'Synthient.lnk'))) {
        if (Test-Path -LiteralPath $lnkPath) {
            # Only remove shortcuts that point into this install.
            $target = $shell.CreateShortcut($lnkPath).TargetPath
            if ($target -and $target.StartsWith($InstallDir + '\', [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $lnkPath -Force
                Say "Removed shortcut $lnkPath"
            }
        }
    }
}

# ---- registry -----------------------------------------------------------------------
if (-not $NoRegistry -and (Test-Path -LiteralPath $UninstKey)) {
    $loc = $null
    try { $loc = (Get-ItemProperty -LiteralPath $UninstKey -Name InstallLocation -ErrorAction Stop).InstallLocation } catch { }
    if (-not $loc -or ([IO.Path]::GetFullPath($loc).TrimEnd('\') -ieq $InstallDir)) {
        Remove-Item -LiteralPath $UninstKey -Recurse -Force
        Say 'Removed the Apps & features entry.'
    }
}

# ---- install folder -----------------------------------------------------------------
if (Test-Path -LiteralPath $InstallDir) {
    # Guard against deleting an unrelated folder.
    if (-not (Test-Path -LiteralPath $exe) -and -not (Test-Path -LiteralPath (Join-Path $InstallDir 'uninstall.ps1'))) {
        throw "$InstallDir does not look like a Synthient install (no Synthient.exe); not deleting it."
    }
    $here = (Get-Location).ProviderPath
    if ($here.StartsWith($InstallDir, [StringComparison]::OrdinalIgnoreCase)) { Set-Location -LiteralPath $env:TEMP }
    try {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
        Say "Removed $InstallDir"
    } catch {
        # Something (e.g. this script's host or an Explorer window) still holds a file:
        # finish the delete from a detached cmd after a short delay.
        Start-Process -FilePath $env:ComSpec -WindowStyle Hidden -ArgumentList "/c ping -n 4 127.0.0.1 >nul & rmdir /s /q `"$InstallDir`""
        Say "Some files were in use; $InstallDir will be removed in a few seconds."
    }
} else {
    Say "$InstallDir not found (already removed)."
}

# ---- user data / API key -------------------------------------------------------------
$userData = Join-Path $env:APPDATA 'Synthient'
if ($RemoveUserData) {
    if (Test-Path -LiteralPath $userData) { Remove-Item -LiteralPath $userData -Recurse -Force; Say "Removed user data $userData" }
} elseif (Test-Path -LiteralPath $userData) {
    Say "Kept your songs, Claude memory and settings in $userData (use -RemoveUserData to delete them)."
}
if ($RemoveApiKey -and [Environment]::GetEnvironmentVariable('ANTHROPIC_API_KEY', 'User')) {
    [Environment]::SetEnvironmentVariable('ANTHROPIC_API_KEY', $null, 'User')
    Say 'Removed the ANTHROPIC_API_KEY user environment variable.'
}

Say 'Synthient uninstalled.'
exit 0
