# Packaging notes (2026-10-30)

## Output

- `packaging\dist\Synthient-0.1.0-win64.zip`: 7,905,191 bytes (7.5 MB), 24 files under one `Synthient/` folder (forward-slash entry names).
  SHA256 `DBF0F7B98F0EFA7AFCFB9BEB5682745C43F77000CE581195DEDC2E3FD1CC6FBF`
- Contains the exe built 2026-10-30 15:31:44, 14,885,376 bytes,
  SHA256 `D157DC7AC88A46E61486B3B32C4B48DBDC349F481B46C3240605DE13247A8192`.
  If the app is rebuilt, rerun `package.ps1`. The zip does not update itself.
- Contents: Synthient.exe, ui (6 files), analysis (12 files: scripts, requirements, setup/run.cmd, README/NOTES, tests\make_test_audio.py), instruments\README.txt, install.ps1, uninstall.ps1, Install Synthient.cmd, version.txt.
- Checked: no `.pdb`, `.venv`, `models`, `__pycache__` or `tests\data` entries.

## Tested (for real, in the scratchpad, without the real Start Menu or registry)

1. `package.ps1`: staged and zipped.
2. Extracted the zip with Expand-Archive and ran `install.ps1 -InstallDir <tmp>\Installed -NoShortcuts -NoRegistry -Quiet`: exit 0. WebView2 Runtime 156.0.4314.50 was detected.
3. Added a marker `<meta>` tag to the *installed* `ui\index.html`, launched the installed exe with `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9334`, and checked `http://127.0.0.1:9334/json`: one page, titled "Synthient" (`https://juce.backend/`). CDP `Runtime.evaluate` (node 20 `--experimental-websocket`) returned `.lane` count 4 and marker present, so the UI was loaded from the install folder. PASS on both the 15:27 build and the final 15:31 build.
4. Closed the app (CloseMainWindow; it exited cleanly) and ran `uninstall.ps1 -InstallDir <tmp>\Installed -NoShortcuts -NoRegistry`: the folder was removed and `%APPDATA%\Synthient` was kept. No HKCU Uninstall key and no Start Menu shortcut existed before or after.
5. Extra: an upgrade over an existing install kept `analysis\.venv`. `uninstall.ps1` with no `-InstallDir`, run from inside the install folder, found and removed its own folder. The guard refused to delete a folder without Synthient.exe.
6. All three .ps1 files parse with no errors.

**Not exercised, only parse-checked:** Start Menu and desktop shortcut creation and removal, writing and removing the HKCU Uninstall registry entry, the interactive ANTHROPIC_API_KEY prompt, `-SetupAnalysis` / `-WithStems` (setup.cmd itself is the existing, tested analysis script), and `Synthient.iss` (Inno Setup is not installed).

## Gotcha: CDP testing while another Synthient is open

Every Synthient instance uses the same WebView2 profile, `%APPDATA%\Synthient\WebView2`. If another copy is already running (for example a dev build with `--remote-debugging-port=9333`), a second instance joins that browser process. Its own debug port never opens, and `/json` returns `[]`. To test anyway, give the test instance its own profile:

```
$env:WEBVIEW2_USER_DATA_FOLDER = "<temp>\wv2profile"
$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9334'
Start-Process <Installed>\Synthient.exe
```

(Verified: the browser process got `--user-data-dir=<temp>\wv2profile\EBWebView --remote-debugging-port=9334`.)

## Design notes

- The app finds `ui\`, `analysis\` and `instruments\` next to the exe (`Source\Paths.h`). It falls back to the source tree only if they are missing, so an installed copy on a dev machine could silently use `D:\Synthient\ui` if its own `ui` were missing. The marker test rules that out.
- install.ps1 refuses to overwrite a running copy, removes the Mark-of-the-Web from installed files, and on upgrade replaces `ui\` but keeps `analysis\.venv`, `analysis\models` and `instruments\`.
- The API key is read with `Read-Host -AsSecureString`, only when the installer is interactive (not `-Quiet`, console input not redirected). It is written with `SetEnvironmentVariable(...,'User')` and never echoed. The BSTR is zeroed afterwards.
- The zip has no code signature, so SmartScreen may warn on first run of the exe or the .cmd.

## For the user

- Set `ANTHROPIC_API_KEY` (the installer offers to) to use the Claude chat.
- Optional: run `analysis\setup.cmd` in the install folder (`--with-stems` for Demucs), or install with `-SetupAnalysis` / `-WithStems`.
- WebView2 is already present on this machine.
- For a setup.exe: install Inno Setup 6 and compile `packaging\Synthient.iss` (see README.md).
