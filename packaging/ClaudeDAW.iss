; Inno Setup 6 script for ClaudeDAW - the equivalent of install.ps1 / uninstall.ps1.
; Not compiled yet. Usage, once Inno Setup 6 is installed:
;   1. powershell -ExecutionPolicy Bypass -File packaging\package.ps1   (stages packaging\dist\ClaudeDAW)
;   2. "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\ClaudeDAW.iss
; Output: packaging\dist\ClaudeDAW-0.1.0-setup.exe
;
; Per-user install (no admin) to %LOCALAPPDATA%\Programs\ClaudeDAW, Start Menu shortcut,
; optional desktop shortcut, Apps & features entry (HKCU), WebView2 check, optional
; ANTHROPIC_API_KEY entry and optional analysis setup. User data in %APPDATA%\ClaudeDAW
; is kept on uninstall.

#define AppName     "ClaudeDAW"
#define AppVersion  "0.1.0"
#define AppPublisher "ClaudeDAW"
#define AppExe      "ClaudeDAW.exe"
#define StageDir    "dist\ClaudeDAW"
#define WebView2Url "https://developer.microsoft.com/microsoft-edge/webview2/"

[Setup]
AppId={{6C1C3F0E-6E55-4B57-9D0B-3C1A2D7DA001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
ChangesEnvironment=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "analysis"; Description: "Set up audio analysis tools now (Python venv, ~550 MB download)"; GroupDescription: "Optional:"; Flags: unchecked
Name: "analysis\stems"; Description: "Include Demucs stem separation (~1.2 GB)"; Flags: unchecked

[Files]
Source: "{#StageDir}\{#AppExe}";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\version.txt";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\uninstall.ps1";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ui\*";             DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\analysis\*";       DestDir: "{app}\analysis"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: ".venv,models,__pycache__,*.pyc,tests\data"
Source: "{#StageDir}\instruments\*";    DestDir: "{app}\instruments"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Dirs]
Name: "{app}\instruments"

[InstallDelete]
; Upgrade: replace the shipped UI; analysis\.venv, models and plug-ins are kept.
Type: filesandordirs; Name: "{app}\ui"

[UninstallDelete]
; Created after install (venv, model cache, plug-ins the user dropped in). %APPDATA%\ClaudeDAW is NOT touched.
Type: filesandordirs; Name: "{app}\analysis\.venv"
Type: filesandordirs; Name: "{app}\analysis\models"
Type: filesandordirs; Name: "{app}\instruments"
Type: filesandordirs; Name: "{app}"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{cmd}"; Parameters: "/c ""{app}\analysis\setup.cmd"""; WorkingDir: "{app}\analysis"; StatusMsg: "Setting up analysis tools (this can take several minutes)..."; Tasks: analysis and not analysis\stems; Flags: waituntilterminated
Filename: "{cmd}"; Parameters: "/c ""{app}\analysis\setup.cmd"" --with-stems"; WorkingDir: "{app}\analysis"; StatusMsg: "Setting up analysis tools with Demucs (this can take a while)..."; Tasks: analysis\stems; Flags: waituntilterminated
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  ApiKeyPage: TInputQueryWizardPage;

function WebView2Installed(): Boolean;
var
  pv: String;
  guid: String;
begin
  guid := '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  Result :=
    (RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\' + guid, 'pv', pv) and (pv <> '') and (pv <> '0.0.0.0')) or
    (RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + guid, 'pv', pv) and (pv <> '') and (pv <> '0.0.0.0')) or
    (RegQueryStringValue(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\' + guid, 'pv', pv) and (pv <> '') and (pv <> '0.0.0.0'));
end;

function ApiKeyAlreadySet(): Boolean;
var
  v: String;
begin
  Result := RegQueryStringValue(HKCU, 'Environment', 'ANTHROPIC_API_KEY', v) and (v <> '');
end;

procedure InitializeWizard();
begin
  ApiKeyPage := CreateInputQueryPage(wpSelectTasks,
    'Anthropic API key', 'The Claude chat needs an Anthropic API key.',
    'Paste your key to save it as the ANTHROPIC_API_KEY user environment variable, or leave it empty to set it later.');
  ApiKeyPage.Add('ANTHROPIC_API_KEY:', True);  { True = password field, never shown }
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = ApiKeyPage.ID) and ApiKeyAlreadySet();
end;

function InitializeSetup(): Boolean;
var
  ErrCode: Integer;
begin
  Result := True;
  if not WebView2Installed() then
  begin
    if SuppressibleMsgBox('Microsoft Edge WebView2 Runtime was not found. ClaudeDAW''s interface needs it.' + #13#10#13#10 +
         'Open the download page now? (Setup will continue.)' + #13#10 + '{#WebView2Url}',
         mbConfirmation, MB_YESNO, IDNO) = IDYES then
      ShellExec('open', '{#WebView2Url}', '', '', SW_SHOWNORMAL, ewNoWait, ErrCode);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Key: String;
begin
  if CurStep = ssPostInstall then
  begin
    Key := Trim(ApiKeyPage.Values[0]);
    if Key <> '' then
      RegWriteStringValue(HKCU, 'Environment', 'ANTHROPIC_API_KEY', Key);
    ApiKeyPage.Values[0] := '';
  end;
end;
