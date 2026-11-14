<#
.SYNOPSIS
  Per-user install of Synthient (no admin rights needed).

.DESCRIPTION
  Copies the package (the folder this script is in) to the install folder,
  adds a Start Menu shortcut and an "Apps & features" uninstall entry under HKCU,
  checks for the WebView2 runtime, and can set ANTHROPIC_API_KEY and set up the
  Python analysis tools.

  User data (songs, Claude memory, settings) lives in %APPDATA%\Synthient and is
  never touched. Re-running over an existing install upgrades it in place and keeps
  analysis\.venv, analysis\models and anything in instruments\.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1 -DesktopShortcut -SetupAnalysis
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1 -InstallDir C:\Temp\CD -NoShortcuts -NoRegistry -Quiet
#>
[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\Synthient'),
    [switch]$DesktopShortcut,     # now the default; kept for old command lines
    [switch]$NoDesktopShortcut,
    [switch]$NoLaunch,
    [string]$OldInstallDir,       # where the old ClaudeDAW is (default: from the registry)
    [switch]$ReplaceOldInstall,   # unattended: move its files over and remove it
    [switch]$NoShortcuts,
    [switch]$NoRegistry,
    [switch]$SetupAnalysis,
    [switch]$WithStems,
    [switch]$SkipApiKey,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$AppName     = 'Synthient'
$Publisher   = 'Synthient'
$UninstKey   = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Synthient'
$WebView2Url = 'https://developer.microsoft.com/microsoft-edge/webview2/'
$SourceDir   = $PSScriptRoot

function Say([string]$Msg)  { if (-not $Quiet) { Write-Host $Msg } }
function Warn([string]$Msg) { Write-Warning $Msg }

function Test-Interactive {
    if ($Quiet) { return $false }
    if (-not [Environment]::UserInteractive) { return $false }
    try { if ([Console]::IsInputRedirected) { return $false } } catch { }
    return $true
}

function Test-WebView2 {
    $guid = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
    $keys = @(
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$guid",
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$guid",
        "HKCU:\Software\Microsoft\EdgeUpdate\Clients\$guid")
    foreach ($k in $keys) {
        try {
            $pv = (Get-ItemProperty -LiteralPath $k -Name pv -ErrorAction Stop).pv
            if ($pv -and $pv -ne '0.0.0.0') { return $pv }
        } catch { }
    }
    return $null
}

# ---- sanity -----------------------------------------------------------------
$SrcExe = Join-Path $SourceDir 'Synthient.exe'
if (-not (Test-Path -LiteralPath $SrcExe)) { throw "Synthient.exe not found next to install.ps1 ($SourceDir). Extract the whole zip first." }
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir 'ui'))) { throw "ui folder missing from the package ($SourceDir)." }

$Version = '0.1.0'
$verFile = Join-Path $SourceDir 'version.txt'
if (Test-Path -LiteralPath $verFile) { $Version = (Get-Content -LiteralPath $verFile -TotalCount 1).Trim() }

# ---- where to install ------------------------------------------------------------
$packageBytes = (Get-ChildItem -LiteralPath $SourceDir -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
$packageGb = [Math]::Round($packageBytes / 1GB, 1)
if (-not $PSBoundParameters.ContainsKey('InstallDir') -and (Test-Interactive)) {
    Say ''
    Say "Synthient needs about $packageGb GB (app, 200+ instruments, Surge XT synth, analysis and stem tools)."
    $answer = Read-Host "Install to $InstallDir ? Press Enter to accept, or type another folder (e.g. D:\Synthient-App)"
    if ($answer.Trim()) { $InstallDir = $answer.Trim().Trim('"') }
}

$InstallDir = [IO.Path]::GetFullPath($InstallDir)
try {
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($InstallDir))
    if ($drive.AvailableFreeSpace -lt $packageBytes * 1.1) {
        throw ("Not enough free space on {0}: {1:N1} GB free, about {2} GB needed. Run the installer again and choose another drive." -f $drive.Name, ($drive.AvailableFreeSpace / 1GB), $packageGb)
    }
} catch [System.ArgumentException] { }
$DstExe     = Join-Path $InstallDir 'Synthient.exe'
Say "Installing $AppName $Version to $InstallDir"

# Refuse to overwrite a running copy.
$running = Get-Process -Name Synthient -ErrorAction SilentlyContinue |
    Where-Object { try { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -ieq $DstExe) } catch { $false } }
if ($running) { throw "Synthient is running from $InstallDir. Close it and run the installer again." }

# ---- copy files ---------------------------------------------------------------
$sameDir = ([IO.Path]::GetFullPath($SourceDir).TrimEnd('\') -ieq $InstallDir.TrimEnd('\'))
if (-not $sameDir) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    # Upgrade: replace the shipped ui and analysis scripts, but keep the venv, models and plug-ins.
    $oldUi = Join-Path $InstallDir 'ui'
    if (Test-Path -LiteralPath $oldUi) { Remove-Item -LiteralPath $oldUi -Recurse -Force }

    # robocopy lists every file with its size (/V /BYTES), which drives a progress bar.
    # Files that are already up to date are listed too, so a reinstall moves quickly.
    $done = [int64]0; $files = 0
    $watch = [Diagnostics.Stopwatch]::StartNew(); $lastDraw = -1000
    & robocopy.exe $SourceDir $InstallDir /E /V /BYTES /NDL /NJH /NJS /NP /R:3 /W:5 /XF 'Install Synthient.cmd' | ForEach-Object {
        if ($_ -match '^\s*(?:[A-Za-z* ]+?)?\s+(\d+)\s+\S') {
            $done += [int64]$Matches[1]; $files++
            if ($watch.ElapsedMilliseconds - $lastDraw -ge 250) {
                $lastDraw = $watch.ElapsedMilliseconds
                $pct = if ($packageBytes -gt 0) { [Math]::Min(100, [int](100 * $done / $packageBytes)) } else { 0 }
                if (-not $Quiet) {
                    Write-Progress -Activity "Installing Synthient to $InstallDir" -Status ("{0}%  -  {1:N1} of {2:N1} GB  ({3:N0} files)" -f $pct, ($done / 1GB), ($packageBytes / 1GB), $files) -PercentComplete $pct
                }
            }
        }
    }
    $copyExit = $LASTEXITCODE
    if (-not $Quiet) { Write-Progress -Activity "Installing Synthient" -Completed }
    if ($copyExit -ge 8) { throw "Copying files failed (robocopy exit $copyExit)." }
    Say ("Copied {0:N0} files in {1:N0} s." -f $files, $watch.Elapsed.TotalSeconds)
    $global:LASTEXITCODE = 0
} else {
    Say 'Package folder is the install folder; skipping copy.'
}
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir 'instruments') | Out-Null

# Files extracted from a downloaded zip carry Mark-of-the-Web; clear it so SmartScreen
# and PowerShell do not block the app or the uninstaller.
Get-ChildItem -LiteralPath $InstallDir -Recurse -File -Include *.exe, *.ps1, *.cmd, *.py, *.js, *.html, *.css -ErrorAction SilentlyContinue |
    Unblock-File -ErrorAction SilentlyContinue

# ---- OB-Xf factory patches ---------------------------------------------------------
# OB-Xf only looks for its patches in %LOCALAPPDATA%\Surge Synth Team\OB-Xf, so copy the
# ones shipped in the full package there. Existing files (the user's own) are left alone.
$obxfSrc = Join-Path $InstallDir 'library\plugins\OB-Xf-assets\Surge Synth Team\OB-Xf'
if (Test-Path -LiteralPath $obxfSrc) {
    $obxfDst = Join-Path $env:LOCALAPPDATA 'Surge Synth Team\OB-Xf'
    & robocopy.exe $obxfSrc $obxfDst /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { Warn "Could not copy the OB-Xf patches (robocopy exit $LASTEXITCODE)." }
    else { Say "OB-Xf patches: $obxfDst" }
    $global:LASTEXITCODE = 0
}

# ---- the old ClaudeDAW install (this app's previous name) -------------------------------
$oldKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ClaudeDAW'
$oldDir = $OldInstallDir
if (-not $oldDir) { try { $oldDir = (Get-ItemProperty -LiteralPath $oldKey -ErrorAction Stop).InstallLocation } catch { } }
if (-not $oldDir) { $oldDir = Join-Path $env:LOCALAPPDATA 'Programs\ClaudeDAW' }
if ((Test-Path -LiteralPath (Join-Path $oldDir 'ClaudeDAW.exe')) -and ([IO.Path]::GetFullPath($oldDir).TrimEnd('\') -ine $InstallDir.TrimEnd('\'))) {
    $oldRunning = Get-Process -Name ClaudeDAW -ErrorAction SilentlyContinue
    $remove = $false
    if ($oldRunning) {
        Warn "The old ClaudeDAW app is still running. Close it and run this installer again to replace it."
    } elseif (Test-Interactive) {
        $ans = Read-Host "Synthient replaces ClaudeDAW (its old name). Move its instruments and tools into Synthient and remove the old app? Your songs, Claude memory and API key are kept. [Y/n]"
        $remove = $ans -notmatch '^(n|no)$'
    } else {
        $remove = [bool]$ReplaceOldInstall   # unattended runs only delete when told to
    }

    # Big folders this package may not include (the small update package): take them from the old
    # install. On the same drive this is a rename, so it's instant.
    $moves = @(
        @{ From = 'library';           To = 'library' },
        @{ From = 'analysis\python';   To = 'analysis\python' },
        @{ From = 'analysis\models';   To = 'analysis\models' })
    foreach ($m in $moves) {
        $from = Join-Path $oldDir $m.From; $to = Join-Path $InstallDir $m.To
        if (-not (Test-Path -LiteralPath $from) -or -not $remove) { continue }
        New-Item -ItemType Directory -Force -Path (Split-Path $to -Parent) | Out-Null
        # The target may already exist with just a few files from this package (e.g. library indexes):
        # then move over whatever it doesn't have yet.
        $pairs = if (Test-Path -LiteralPath $to) {
            Get-ChildItem -LiteralPath $from -Force | Where-Object { -not (Test-Path -LiteralPath (Join-Path $to $_.Name)) } |
                ForEach-Object { @{ From = $_.FullName; To = (Join-Path $to $_.Name) } }
        } else { @(@{ From = $from; To = $to }) }
        foreach ($pr in $pairs) {
            try { Move-Item -LiteralPath $pr.From -Destination $pr.To -ErrorAction Stop }
            catch { Warn "Couldn't move $($pr.From) ($($_.Exception.Message)); copying instead."; & robocopy.exe $pr.From $pr.To /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null; $global:LASTEXITCODE = 0 }
        }
        Say "Moved $($m.From) from the old ClaudeDAW install."
    }
    # Kept the old app: let Synthient use its library and tools where they are.
    if (-not $remove) {
        $appDataNew = Join-Path $env:APPDATA 'Synthient'
        New-Item -ItemType Directory -Force -Path $appDataNew | Out-Null
        if (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'library\index.json')) -and (Test-Path -LiteralPath (Join-Path $oldDir 'library\index.json'))) {
            Set-Content -LiteralPath (Join-Path $appDataNew 'library_path.txt') -Value (Join-Path $oldDir 'library') -Encoding UTF8 -NoNewline
        }
        if (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'analysis\python\python.exe')) -and (Test-Path -LiteralPath (Join-Path $oldDir 'analysis\python\python.exe'))) {
            Set-Content -LiteralPath (Join-Path $appDataNew 'analysis_path.txt') -Value (Join-Path $oldDir 'analysis') -Encoding UTF8 -NoNewline
        }
    }

    if ($remove) {
        Remove-Item -LiteralPath $oldDir -Recurse -Force -ErrorAction SilentlyContinue
        foreach ($lnk in $(if ($OldInstallDir) { @() } else { @((Join-Path ([Environment]::GetFolderPath('Programs')) 'ClaudeDAW.lnk'), (Join-Path ([Environment]::GetFolderPath('Desktop')) 'ClaudeDAW.lnk')) })) {
            if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force -ErrorAction SilentlyContinue }
        }
        if (-not $OldInstallDir) { Remove-Item -LiteralPath $oldKey -Recurse -Force -ErrorAction SilentlyContinue }
        if (Test-Path -LiteralPath $oldDir) { Warn "Some old files at $oldDir couldn't be removed; delete that folder by hand." }
        else { Say 'Removed the old ClaudeDAW app.' }
    }
}

# ---- shortcuts ------------------------------------------------------------------
if (-not $NoShortcuts) {
    $shell = New-Object -ComObject WScript.Shell
    $targets = @(Join-Path ([Environment]::GetFolderPath('Programs')) 'Synthient.lnk')
    if (-not $NoDesktopShortcut) { $targets += Join-Path ([Environment]::GetFolderPath('Desktop')) 'Synthient.lnk' }
    foreach ($lnkPath in $targets) {
        $lnk = $shell.CreateShortcut($lnkPath)
        $lnk.TargetPath       = $DstExe
        $lnk.WorkingDirectory = $InstallDir
        $lnk.IconLocation     = "$DstExe,0"
        $lnk.Description      = 'Synthient - DAW with Claude built in'
        $lnk.Save()
        Say "Shortcut: $lnkPath"
    }
}

# ---- uninstall registry entry -------------------------------------------------
if (-not $NoRegistry) {
    $uninst = Join-Path $InstallDir 'uninstall.ps1'
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $sizeKb = [int]((Get-ChildItem -LiteralPath $InstallDir -Recurse -File -Force -ErrorAction SilentlyContinue |
        Measure-Object Length -Sum).Sum / 1KB)
    New-Item -Path $UninstKey -Force | Out-Null
    $props = [ordered]@{
        DisplayName          = $AppName
        DisplayVersion       = $Version
        Publisher            = $Publisher
        InstallLocation      = $InstallDir
        DisplayIcon          = "$DstExe,0"
        UninstallString      = "`"$ps`" -NoProfile -ExecutionPolicy Bypass -File `"$uninst`""
        QuietUninstallString = "`"$ps`" -NoProfile -ExecutionPolicy Bypass -File `"$uninst`" -Quiet"
        InstallDate          = (Get-Date -Format 'yyyyMMdd')
    }
    foreach ($p in $props.GetEnumerator()) {
        New-ItemProperty -Path $UninstKey -Name $p.Key -Value $p.Value -PropertyType String -Force | Out-Null
    }
    New-ItemProperty -Path $UninstKey -Name EstimatedSize -Value $sizeKb -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $UninstKey -Name NoModify      -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $UninstKey -Name NoRepair      -Value 1 -PropertyType DWord -Force | Out-Null
    Say 'Registered in Settings > Apps (per user).'
}

# ---- WebView2 ---------------------------------------------------------------------
$wv = Test-WebView2
if ($wv) { Say "Microsoft Edge WebView2 Runtime $wv found." }
else {
    Warn "Microsoft Edge WebView2 Runtime was not found. Synthient's interface needs it. Install the Evergreen runtime from $WebView2Url"
}

# ---- Anthropic API key --------------------------------------------------------------
$existingKey = [Environment]::GetEnvironmentVariable('ANTHROPIC_API_KEY', 'User')
if (-not $existingKey) { $existingKey = [Environment]::GetEnvironmentVariable('ANTHROPIC_API_KEY', 'Machine') }
$hasKey = [bool]$existingKey
$existingKey = $null
if (-not $SkipApiKey -and (Test-Interactive)) {
    Say ''
    $ans = if ($hasKey) { Read-Host 'An Anthropic API key is already set. Replace it with a new one? [y/N]' }
           else         { Read-Host 'The Claude chat needs an Anthropic API key. Enter it now? [Y/n]' }
    $wantKey = if ($hasKey) { $ans -match '^(y|yes)$' } else { $ans -notmatch '^(n|no)$' }
    if ($wantKey) {
        $secure = Read-Host 'Paste your Anthropic API key (input hidden)' -AsSecureString
        $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        try {
            $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
            if ($plain -and $plain.Trim()) {
                [Environment]::SetEnvironmentVariable('ANTHROPIC_API_KEY', $plain.Trim(), 'User')
                $env:ANTHROPIC_API_KEY = $plain.Trim()   # so 'Start Synthient now' below sees it too
                Say 'ANTHROPIC_API_KEY saved as a user environment variable (applies to newly started programs).'
            } else { Say 'No key entered; skipped.' }
        } finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
            $plain = $null
            $secure.Dispose()
        }
    } elseif (-not $hasKey) {
        Say 'Skipped. Run the installer again (or set ANTHROPIC_API_KEY as a user environment variable) to use the Claude chat.'
    }
} elseif (-not $hasKey) {
    Say 'Note: ANTHROPIC_API_KEY is not set; the Claude chat needs it (user environment variable).'
}

# ---- point the app at this install's library and analysis tools ----------------------
$appData = Join-Path $env:APPDATA 'Synthient'
New-Item -ItemType Directory -Force -Path $appData | Out-Null
$lib = Join-Path $InstallDir 'library'
if (Test-Path -LiteralPath (Join-Path $lib 'index.json')) {
    Set-Content -LiteralPath (Join-Path $appData 'library_path.txt') -Value $lib -Encoding UTF8 -NoNewline
    Say "Instrument library: $lib"
}
if (Test-Path -LiteralPath (Join-Path $InstallDir 'analysis\python\python.exe')) {
    $pointer = Join-Path $appData 'analysis_path.txt'
    if (Test-Path -LiteralPath $pointer) { Remove-Item -LiteralPath $pointer -Force }
    Say 'Analysis and stem separation tools: included.'
}

# ---- analysis tools (optional) -----------------------------------------------------
if (($SetupAnalysis -or $WithStems) -and -not (Test-Path -LiteralPath (Join-Path $InstallDir 'analysis\python\python.exe'))) {
    $setup = Join-Path $InstallDir 'analysis\setup.cmd'
    if (-not (Test-Path -LiteralPath $setup)) { throw "analysis\setup.cmd missing from $InstallDir" }
    Say ('Setting up analysis tools' + $(if ($WithStems) { ' with Demucs stems (about 1.2 GB)' } else { ' (about 550 MB)' }) + '...')
    $cmdArgs = '/c ""' + $setup + '"' + $(if ($WithStems) { ' --with-stems' } else { '' }) + '"'
    $p = Start-Process -FilePath $env:ComSpec -ArgumentList $cmdArgs -WorkingDirectory (Join-Path $InstallDir 'analysis') -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Warn "analysis\setup.cmd failed (exit $($p.ExitCode)). The DAW works without it; rerun $setup later." }
    else { Say 'Analysis tools ready.' }
}

Say ''
Say "$AppName $Version installed to $InstallDir"
Say "Run: $DstExe"
if (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'analysis\python\python.exe')) -and -not (Test-Path -LiteralPath (Join-Path $InstallDir 'analysis\.venv'))) {
    Say "Optional: audio analysis tools -> `"$InstallDir\analysis\setup.cmd`" (add --with-stems for Demucs)"
}
if (-not $NoLaunch -and (Test-Interactive)) {
    $go = Read-Host 'Start Synthient now? [Y/n]'
    if ($go -notmatch '^(n|no)$') {
        # A fresh process so it sees a key that was just set.
        Start-Process -FilePath $DstExe -WorkingDirectory $InstallDir
    }
}
exit 0
