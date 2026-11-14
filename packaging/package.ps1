<#
.SYNOPSIS
  Stages Synthient into packaging\dist\Synthient\ and zips it as
  packaging\dist\Synthient-<version>-win64.zip.

.DESCRIPTION
  Does not build anything. The default package has the built Release exe (no .pdb), ui\,
  the Starter Kit, the analysis tools as a self-contained Python (librosa, Basic Pitch,
  Demucs + its model), the instrument library's indexes and tools (no plug-ins or samples:
  install.ps1 downloads those from each project with library\tools\get_library.py),
  an empty instruments\ folder, and the installer scripts from this folder.
  -WithLibrary also bundles the whole instrument library (about 5 GB) for offline installs.
  -AppOnly leaves out the library and Python for a quick update package.

.PARAMETER Version
  Version stamped into the package name and version.txt. Default 0.1.0.

.PARAMETER ExePath
  Built exe. Default: build\app\ClaudeDAW_artefacts\Release\Synthient.exe.
#>
[CmdletBinding()]
param(
    [string]$Version = '0.1.0',
    [string]$ExePath,
    [switch]$AppOnly,
    [switch]$WithLibrary   # bundle the full instrument library instead of downloading it at install
)
if ($AppOnly -and $WithLibrary) { throw '-AppOnly and -WithLibrary cannot be combined.' }

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$PackagingDir = $PSScriptRoot
$RepoRoot     = Split-Path $PackagingDir -Parent
if (-not $ExePath) { $ExePath = Join-Path $RepoRoot 'build\app\ClaudeDAW_artefacts\Release\Synthient.exe' }

$Dist    = Join-Path $PackagingDir 'dist'
$Stage   = Join-Path $Dist $(if ($AppOnly) { 'Synthient-update' } else { 'Synthient' })
$ZipPath = Join-Path $Dist ("Synthient-$Version-win64" + $(if ($AppOnly) { '-apponly' } elseif ($WithLibrary) { '-full' } else { '' }) + '.zip')

function Copy-WithRetry([string]$Src, [string]$Dst, [int]$Tries = 30) {
    for ($i = 1; $i -le $Tries; $i++) {
        try { Copy-Item -LiteralPath $Src -Destination $Dst -Force; return }
        catch {
            if ($i -eq $Tries) { throw }
            Write-Host "  $([IO.Path]::GetFileName($Src)) is locked (build in progress?), retrying in 10 s ($i/$Tries)..."
            Start-Sleep -Seconds 10
        }
    }
}

function Invoke-Robocopy([string]$Src, [string]$Dst, [string[]]$Extra) {
    & robocopy.exe $Src $Dst /E /NFL /NDL /NJH /NJS /NP /R:3 /W:5 @Extra | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $Src -> $Dst failed (exit $LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
}

if (-not (Test-Path -LiteralPath $ExePath)) { throw "Built exe not found: $ExePath (build the Release target first)" }
foreach ($req in 'ui', 'analysis') {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $req))) { throw "Missing $req folder in $RepoRoot" }
}

Write-Host "Staging Synthient $Version -> $Stage"
if (Test-Path -LiteralPath $Stage)   { Remove-Item -LiteralPath $Stage -Recurse -Force }
if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

# 1. App exe (the .pdb is never copied).
Copy-WithRetry $ExePath (Join-Path $Stage 'Synthient.exe')

# 2. UI (HTML/JS/CSS), required at runtime.
Invoke-Robocopy (Join-Path $RepoRoot 'ui') (Join-Path $Stage 'ui') @('/XD', '__pycache__', 'node_modules')

# 2b. How to connect Claude Code (docs/CLAUDE_CODE.md), next to the exe.
Copy-Item -LiteralPath (Join-Path $RepoRoot 'docs\CLAUDE_CODE.md') -Destination (Join-Path $Stage 'CLAUDE_CODE.md')

# 3. Analysis scripts only.
$AnaSrc = Join-Path $RepoRoot 'analysis'
Invoke-Robocopy $AnaSrc (Join-Path $Stage 'analysis') @(
    '/XD', (Join-Path $AnaSrc '.venv'), (Join-Path $AnaSrc 'models'), (Join-Path $AnaSrc 'tests\data'), '__pycache__', '.pytest_cache',
    '/XF', '*.pyc', '*.pyo')

# 3b. Built-in Starter Kit drum samples and their index (the generator script stays out).
Invoke-Robocopy (Join-Path $RepoRoot 'samples') (Join-Path $Stage 'samples') @('/XF', '*.py')

# 3c. Instrument library: every package carries the small index files, the download list and
#     the library tools (downloader, checker, GeneralUser converter), which robocopy merges into
#     an existing library folder on install.
$LibSrc = Join-Path $RepoRoot 'library'
New-Item -ItemType Directory -Force -Path (Join-Path $Stage 'library') | Out-Null
foreach ($f in 'index.json', 'surge_patches.json', 'obxf_patches.json', 'sources.json', 'NOTES.md') {
    $src = Join-Path $LibSrc $f
    if (-not (Test-Path -LiteralPath $src)) { throw "Missing library\$f" }
    Copy-Item -LiteralPath $src -Destination (Join-Path $Stage "library\$f")
}
Invoke-Robocopy (Join-Path $LibSrc 'tools') (Join-Path $Stage 'library\tools') @('/XD', 'sfizz', '__pycache__', '/XF', '*.pyc')

# 3c-2. -WithLibrary: the plug-ins and samples themselves (about 5 GB).
if ($WithLibrary) {
    # The old 3-second GeneralUser renders are superseded by sfz/GeneralUser-GS.
    Invoke-Robocopy $LibSrc (Join-Path $Stage 'library') @('/XD', 'tools', '__pycache__', 'downloads', '.downloads', 'GeneralUser-GS-extracted', '/XF', 'index.json.bak*', 'extract.log')
}

# 3d. Analysis tools as a self-contained Python: the base interpreter the dev venv was made from,
#     plus the venv's packages, plus the Demucs model. A copied venv wouldn't work elsewhere.
if (-not $AppOnly) {
    $venv = Join-Path $AnaSrc '.venv'
    $cfg = Get-Content -LiteralPath (Join-Path $venv 'pyvenv.cfg') -ErrorAction Stop
    $pyHome = (($cfg | Where-Object { $_ -match '^\s*home\s*=' }) -replace '^\s*home\s*=\s*', '').Trim()
    if (-not (Test-Path -LiteralPath (Join-Path $pyHome 'python.exe'))) { throw "Base Python for analysis\.venv not found at $pyHome" }
    $py = Join-Path $Stage 'analysis\python'
    Write-Host "  Bundling Python from $pyHome"
    Invoke-Robocopy $pyHome $py @('/XD', '__pycache__', '/XF', '*.pyc')
    Invoke-Robocopy (Join-Path $venv 'Lib\site-packages') (Join-Path $py 'Lib\site-packages') @('/XD', '__pycache__', '/XF', '*.pyc')
    Invoke-Robocopy (Join-Path $AnaSrc 'models') (Join-Path $Stage 'analysis\models') @()
}

# 4. Empty instruments folder for VST3 plug-ins (with a note so it is never dropped).
$Inst = Join-Path $Stage 'instruments'
New-Item -ItemType Directory -Force -Path $Inst | Out-Null
Set-Content -LiteralPath (Join-Path $Inst 'README.txt') -Encoding ASCII -Value @(
    'Put VST3 plug-ins (.vst3) here. Synthient scans this folder and Common Files\VST3',
    'from the Plug-ins dialog.')

# 5. Installer scripts and metadata.
foreach ($f in 'install.ps1', 'uninstall.ps1', 'Install Synthient.cmd', 'Get Instruments.cmd') {
    Copy-Item -LiteralPath (Join-Path $PackagingDir $f) -Destination (Join-Path $Stage $f) -Force
}
Set-Content -LiteralPath (Join-Path $Stage 'version.txt') -Value $Version -Encoding ASCII
$license = Join-Path $RepoRoot 'LICENSE'
if (Test-Path -LiteralPath $license) { Copy-Item -LiteralPath $license -Destination $Stage }

# Sanity check: nothing that must not ship.
$bad = Get-ChildItem -LiteralPath $Stage -Recurse -Force |
    Where-Object { $_.Name -like '*.pdb' -or $_.Name -in '.venv', '__pycache__' }
if ($bad) { throw "Forbidden items staged: $($bad.FullName -join ', ')" }

# Zip with a top-level Synthient/ folder. Entries are added one by one so their names
# use forward slashes (.NET Framework's CreateFromDirectory writes backslashes, which
# non-Windows unzip tools treat as part of the file name).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zipArc = [IO.Compression.ZipFile]::Open($ZipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    $prefixLen = (Split-Path $Stage -Parent).TrimEnd('\').Length + 1
    foreach ($file in Get-ChildItem -LiteralPath $Stage -Recurse -File -Force) {
        $entryName = $file.FullName.Substring($prefixLen).Replace('\', '/')
        # Audio barely compresses; store it quickly so the full package doesn't take ages.
        $level = if ($file.Extension -match '^\.(wav|flac|ogg|aif|aiff|sf2|dll|pyd|lib|th|safetensors|bin|vst3|exe)$') { [IO.Compression.CompressionLevel]::Fastest } else { [IO.Compression.CompressionLevel]::Optimal }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zipArc, $file.FullName, $entryName, $level) | Out-Null
    }
} finally { $zipArc.Dispose() }

$zip = Get-Item -LiteralPath $ZipPath
$files = (Get-ChildItem -LiteralPath $Stage -Recurse -File).Count
Write-Host ("Done: {0} ({1:N1} MB, {2} files)" -f $zip.FullName, ($zip.Length / 1MB), $files)
