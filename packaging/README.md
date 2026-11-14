# Synthient packaging

Builds a zip that installs Synthient per user (no admin rights, no extra tools).

| File | Purpose |
|---|---|
| `package.ps1` | Stages the built app into `dist\Synthient\` and writes `dist\Synthient-<version>-win64.zip` |
| `install.ps1` | Per-user installer (shipped in the zip) |
| `uninstall.ps1` | Uninstaller (shipped in the zip, copied into the install folder) |
| `Install Synthient.cmd` | Double-click wrapper: runs `install.ps1` with `-ExecutionPolicy Bypass` |
| `Get Instruments.cmd` | Downloads or finishes the instrument library with the bundled Python (`library\tools\get_library.py`, list in `library\sources.json`) |
| `Synthient.iss` | Inno Setup 6 script producing a `setup.exe` (optional, needs Inno Setup) |

## Package

1. Build the Release app first (see the top-level README). `package.ps1` does not build.
2. Run:

```
powershell -ExecutionPolicy Bypass -File packaging\package.ps1 [-Version 0.1.0] [-ExePath <exe>] [-WithLibrary | -AppOnly]
```

| Package | Zip | Library |
|---|---|---|
| default | `Synthient-<v>-win64.zip` | Indexes and tools only; the installer downloads the plug-ins and samples |
| `-WithLibrary` | `Synthient-<v>-win64-full.zip` (about 5 GB) | Bundled, for offline installs |
| `-AppOnly` | `Synthient-<v>-win64-apponly.zip` | Indexes and tools only, and no Python: an update over an existing install |

The zip has one top-level `Synthient\` folder containing:

- `Synthient.exe` (statically linked; the `.pdb` is not shipped)
- `ui\` (HTML/JS/CSS interface, required)
- `analysis\` (Python scripts, plus a self-contained Python and the Demucs model unless `-AppOnly`)
- `library\` (indexes, `sources.json`, `tools\`; plus the plug-ins and samples with `-WithLibrary`)
- `instruments\` (empty folder for VST3 plug-ins, with a README.txt)
- `install.ps1`, `uninstall.ps1`, `Install Synthient.cmd`, `Get Instruments.cmd`, `version.txt`

If `Synthient.exe` is locked by a running build, the script retries for up to 5 minutes.

## Install

Extract the zip and double-click **Install Synthient.cmd**, or run:

```
powershell -ExecutionPolicy Bypass -File install.ps1 [options]
```

| Option | Effect |
|---|---|
| `-InstallDir <dir>` | Install folder. Default `%LOCALAPPDATA%\Programs\Synthient` |
| `-DesktopShortcut` | Also add a desktop shortcut (a Start Menu shortcut is always added) |
| `-SetupAnalysis` | Run `analysis\setup.cmd` in the install folder (Python venv, about 550 MB) |
| `-WithStems` | Same, with `--with-stems` (adds Demucs, about 1.2 GB) |
| `-SkipApiKey` | Do not ask for the Anthropic API key |
| `-GetLibrary` | Download the instrument library without asking (also with `-Quiet`) |
| `-SkipLibrary` | Don't download it (get it later with Get Instruments) |
| `-NoShortcuts` / `-NoRegistry` | Skip shortcuts / the Apps & features entry (for testing) |
| `-Quiet` | No prompts and minimal output (warnings still shown) |

What it does:

- Copies the files. Re-running over an existing install upgrades it: `ui\` is replaced, and `analysis\.venv`, `analysis\models` and plug-ins in `instruments\` are kept. It refuses to run while that copy of Synthient is open.
- Removes the downloaded-file mark (Mark-of-the-Web) from the installed files.
- Offers to download the instrument library parts that neither the package nor the existing install has (`library\tools\get_library.py` with the bundled Python). A failed or skipped download only warns; **Get Instruments** (Start Menu) finishes it later. Then copies the OB-Xf patches to `%LOCALAPPDATA%\Surge Synth Team\OB-Xf`, where the plug-in looks for them.
- Adds a Start Menu shortcut, and an uninstall entry in `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Synthient`, so it appears in Settings > Apps.
- Checks for the Microsoft Edge WebView2 Runtime (EdgeUpdate client `{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` under HKLM and HKCU). If it is missing, it warns with https://developer.microsoft.com/microsoft-edge/webview2/ . Windows 10/11 normally have it.
- If `ANTHROPIC_API_KEY` is not set and the installer is interactive, it offers to set it. The key is read with hidden input and saved as a user environment variable; it is never printed or logged. Programs started after that see it.

User data (songs, Claude memory, settings) is in `%APPDATA%\Synthient`, outside the install folder, so installing or upgrading never touches it.

## Uninstall

Use Settings > Apps > Synthient > Uninstall, or run:

```
powershell -ExecutionPolicy Bypass -File "%LOCALAPPDATA%\Programs\Synthient\uninstall.ps1" [options]
```

| Option | Effect |
|---|---|
| `-InstallDir <dir>` | Folder to remove. Default: the script's own folder, else the registered location |
| `-RemoveUserData` | Also delete `%APPDATA%\Synthient` (songs, Claude memory). Kept by default |
| `-RemoveApiKey` | Also remove the `ANTHROPIC_API_KEY` user variable. Kept by default |
| `-NoShortcuts` / `-NoRegistry` / `-Quiet` | As for install |

It removes the install folder (including the analysis venv and anything in `instruments\`), the Start Menu and desktop shortcuts that point into it, and the registry entry. It will not delete a folder that has no `Synthient.exe` or `uninstall.ps1`.

## Inno Setup (optional)

After installing Inno Setup 6:

```
powershell -ExecutionPolicy Bypass -File packaging\package.ps1
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\Synthient.iss
```

This writes `packaging\dist\Synthient-0.1.0-setup.exe`, which does the same per-user install, with wizard pages for the desktop shortcut, the analysis setup and the API key (a password field). Update `AppVersion` in the `.iss` when the version changes. The script has not been compiled yet.
