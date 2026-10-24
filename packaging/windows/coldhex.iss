; Inno Setup script of the ColdHex installer for Windows.
; It installs for the current user only (no administrator rights, no UAC window) into
; %LOCALAPPDATA%\Programs\ColdHex, so that the program can update itself without asking.
;
; Build it from the installed tree of the build (cmake --install build --prefix stage):
;   ISCC /DAppVersion=1.0.2 /DSourceDir=<stage folder> /DOutputDir=<folder> coldhex.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build"
#endif

[Setup]
AppId={{C4BD8E7A-C444-447C-919C-8F3A259A8BE9}
AppName=ColdHex
AppVersion={#AppVersion}
AppPublisher=ColdHex Project
AppPublisherURL=https://github.com/hakanyz/coldhex
AppSupportURL=https://github.com/hakanyz/coldhex/issues
AppUpdatesURL=https://github.com/hakanyz/coldhex/releases
DefaultDirName={autopf}\ColdHex
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=coldhex-{#AppVersion}-win64
SetupIconFile=..\..\resources\icons\coldhex.ico
UninstallDisplayIcon={app}\bin\coldhex.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ColdHex"; Filename: "{app}\bin\coldhex.exe"
Name: "{autodesktop}\ColdHex"; Filename: "{app}\bin\coldhex.exe"; Tasks: desktopicon

[Run]
; After a normal installation the wizard offers to start the program.
Filename: "{app}\bin\coldhex.exe"; Description: "{cm:LaunchProgram,ColdHex}"; Flags: nowait postinstall skipifsilent
; An update started by ColdHex itself (/SILENT /RELAUNCH) opens the new version again.
Filename: "{app}\bin\coldhex.exe"; Flags: nowait runasoriginaluser; Check: IsSilentRelaunch

[Code]
function CmdLineParamExists(const Value: string): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Value) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function IsSilentRelaunch: Boolean;
begin
  Result := WizardSilent() and CmdLineParamExists('/RELAUNCH');
end;

// Versions up to 1.0.1 were installed for all users into Program Files by another installer.
// That copy is not replaced by this one, so two copies would stay side by side. Offer to remove it.
// The old installer was a 32-bit program, so its entry is in the 32-bit view of the registry.
const
  OldUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\ColdHex';

function FindOldUninstaller(var UninstallString: string): Boolean;
begin
  Result := RegQueryStringValue(HKLM32, OldUninstallKey, 'UninstallString', UninstallString)
         or RegQueryStringValue(HKLM64, OldUninstallKey, 'UninstallString', UninstallString);
  if Result then
  begin
    UninstallString := RemoveQuotes(UninstallString);
    Result := FileExists(UninstallString);
  end;
end;

function InitializeSetup: Boolean;
var
  UninstallString: string;
  ResultCode: Integer;
begin
  Result := True;
  if WizardSilent() then Exit;
  if not FindOldUninstaller(UninstallString) then Exit;

  if MsgBox('An older ColdHex that was installed for all users is still on this computer. ' +
            'It is not replaced by this installation, so both would remain.' + #13#10#13#10 +
            'Remove the older one now? (Windows asks for permission. Your profiles and settings are kept.)',
            mbConfirmation, MB_YESNO) = IDYES then
    ShellExec('runas', UninstallString, '/S', '', SW_SHOW, ewNoWait, ResultCode);
end;