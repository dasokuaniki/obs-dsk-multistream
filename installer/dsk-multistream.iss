#ifndef SourceRoot
  #error SourceRoot must point to the staged plugin package.
#endif
#ifndef OutputDir
  #error OutputDir must point to the installer output directory.
#endif
#ifndef AppVersion
  #error AppVersion must be supplied by the packaging script.
#endif
#ifndef AppId
  #error AppId must be supplied by the packaging script.
#endif
#ifndef LicenseFile
  #error LicenseFile must point to the project license.
#endif
#ifndef PrivacyFile
  #error PrivacyFile must point to the project privacy policy.
#endif

#define AppPublisher "DSK"
#define ProductName "DSK Multistream"
#define PluginDirectoryName "obs-dsk-multistream"
#ifdef TestMode
  #define AppName "DSK Multistream for OBS (Installer E2E)"
  #define OutputName "DSK-Multistream-" + AppVersion + "-Windows-x64-E2E-Setup"
#else
  #define AppName ProductName
  #define OutputName "DSK-Multistream-" + AppVersion + "-Windows-x64-Setup"
#endif

[Setup]
AppId={{{#AppId}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=Copyright (C) 2026 DSK
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginDirectoryName}
DisableDirPage=yes
DisableProgramGroupPage=yes
AllowNoIcons=yes
#ifdef TestMode
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile={#LicenseFile}
InfoBeforeFile={#PrivacyFile}
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
Uninstallable=yes
CreateUninstallRegKey=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={uninstallexe}
#ifdef ReleaseSignTool
SignTool=dsk_release
SignedUninstaller=yes
SignToolRetryCount=0
#endif
#ifdef ExternalSignedUninstallerDir
SignedUninstaller=yes
SignedUninstallerDir={#ExternalSignedUninstallerDir}
#endif
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoCopyright=Copyright (C) 2026 DSK
VersionInfoDescription={#AppName} installer
VersionInfoOriginalFileName={#OutputName}.exe
VersionInfoProductName={#ProductName}
VersionInfoProductTextVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}
VersionInfoTextVersion={#AppVersion}.0
ChangesAssociations=no
ChangesEnvironment=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
english.ObsNotFound=OBS Studio 32 or later was not found. Install the 64-bit version of OBS Studio before installing DSK Multistream.
japanese.ObsNotFound=OBS Studio 32 以降が見つかりません。64-bit版OBS Studioをインストールしてから、DSK Multistreamをインストールしてください。
english.ObsTooOld=DSK Multistream requires OBS Studio 32 or later. The detected OBS installation is version %1.
japanese.ObsTooOld=DSK MultistreamにはOBS Studio 32以降が必要です。検出されたOBSのバージョンは%1です。
english.CompleteRemovalPrompt=Also remove all DSK Multistream destinations, layouts, stream keys, and account authorizations saved for this Windows account?%n%nYes: complete removal%nNo: keep settings for reinstall%nCancel: do not uninstall
japanese.CompleteRemovalPrompt=このWindowsアカウントに保存したDSK Multistreamの配信先、レイアウト、ストリームキー、アカウント認証も削除しますか？%n%nはい：完全削除%nいいえ：再インストール用に設定を保持%nキャンセル：アンインストールしない
english.CompleteRemovalFailed=Complete removal could not safely finish. Uninstall was stopped so the problem can be reviewed. No other product data was intentionally removed.
japanese.CompleteRemovalFailed=完全削除を安全に完了できなかったため、確認できるようアンインストールを中止しました。他製品のデータは意図的に削除していません。

[InstallDelete]
Type: filesandordirs; Name: "{app}\bin\64bit\tls"

[Files]
Source: "{#SourceRoot}\bin\64bit\obs-dsk-multistream.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SourceRoot}\data\locale\*"; DestDir: "{app}\data\locale"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\data\presets\*"; DestDir: "{app}\data\presets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\data\ui\*"; DestDir: "{app}\data\ui"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\tools\*"; DestDir: "{app}\tools"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\dsk-package-manifest.json"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
; This dedicated plugin directory contains binaries and static data only.
; Per-profile settings and Windows Credential Manager entries live elsewhere and are intentionally preserved.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\docs"
Type: filesandordirs; Name: "{app}\tools"
Type: files; Name: "{app}\LICENSE"
Type: files; Name: "{app}\dsk-package-manifest.json"
Type: dirifempty; Name: "{app}"

[Code]
function HasCommandLineParameter(const Value: String): Boolean;
var
  Index: Integer;
begin
  Result := False;
  for Index := 1 to ParamCount do
  begin
    if CompareText(ParamStr(Index), Value) = 0 then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function RunCompleteRemoval(): Boolean;
var
  PowerShellPath: String;
  ScriptPath: String;
  Arguments: String;
  ResultCode: Integer;
#ifdef TestMode
  TestRoot: String;
  ProfilesRoot: String;
  ModuleConfigRoot: String;
#endif
begin
  Result := False;
  PowerShellPath := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  ScriptPath := ExpandConstant('{app}\tools\remove-user-data.ps1');
  if not FileExists(PowerShellPath) or not FileExists(ScriptPath) then
    Exit;

  Arguments := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    AddQuotes(ScriptPath) + ' -Force';
#ifdef TestMode
  TestRoot := ExpandConstant('{app}\..');
  ProfilesRoot := TestRoot + '\dsk-e2e-user-data\profiles';
  ModuleConfigRoot := TestRoot + '\dsk-e2e-user-data\module-config';
  Arguments := Arguments + ' -TestMode -TestRoot ' + AddQuotes(TestRoot) +
    ' -ProfilesRoot ' + AddQuotes(ProfilesRoot) +
    ' -ModuleConfigRoot ' + AddQuotes(ModuleConfigRoot) +
    ' -CredentialPrefix "DSK Multistream E2E/"';
#endif

  if not Exec(PowerShellPath, Arguments, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Exit;
  Result := ResultCode = 0;
end;

function InitializeUninstall(): Boolean;
var
  CompleteRemoval: Boolean;
  SilentUninstall: Boolean;
  Choice: Integer;
begin
  Result := False;
  CompleteRemoval := HasCommandLineParameter('/DSKCOMPLETE=1');
  SilentUninstall := HasCommandLineParameter('/SILENT') or
    HasCommandLineParameter('/VERYSILENT');

  if not CompleteRemoval and not SilentUninstall then
  begin
    Choice := MsgBox(CustomMessage('CompleteRemovalPrompt'), mbConfirmation, MB_YESNOCANCEL);
    if Choice = IDCANCEL then
      Exit;
    CompleteRemoval := Choice = IDYES;
  end;

  if CompleteRemoval and not RunCompleteRemoval() then
  begin
    if not SilentUninstall then
      MsgBox(CustomMessage('CompleteRemovalFailed'), mbError, MB_OK)
    else
      Log(CustomMessage('CompleteRemovalFailed'));
    Exit;
  end;

  Result := True;
end;

function DetectObsExecutable(): String;
var
  InstallPath: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', InstallPath) or
     RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', InstallPath) then
  begin
    Result := AddBackslash(InstallPath) + 'bin\64bit\obs64.exe';
    if FileExists(Result) then
      Exit;
  end;

  Result := ExpandConstant('{autopf}\obs-studio\bin\64bit\obs64.exe');
  if not FileExists(Result) then
    Result := '';
end;

function InitializeSetup(): Boolean;
var
  ObsExecutable: String;
  ObsMS, ObsLS: Cardinal;
  ObsMajor, ObsMinor, ObsPatch: Cardinal;
  DetectedVersion: String;
begin
  Result := False;
  ObsExecutable := DetectObsExecutable();
  if ObsExecutable = '' then
  begin
    MsgBox(CustomMessage('ObsNotFound'), mbError, MB_OK);
    Exit;
  end;

  if not GetVersionNumbers(ObsExecutable, ObsMS, ObsLS) then
  begin
    MsgBox(CustomMessage('ObsNotFound'), mbError, MB_OK);
    Exit;
  end;

  ObsMajor := ObsMS shr 16;
  ObsMinor := ObsMS and $FFFF;
  ObsPatch := ObsLS shr 16;
  if ObsMajor < 32 then
  begin
    DetectedVersion := Format('%d.%d.%d', [ObsMajor, ObsMinor, ObsPatch]);
    MsgBox(FmtMessage(CustomMessage('ObsTooOld'), [DetectedVersion]), mbError, MB_OK);
    Exit;
  end;

  Result := True;
end;
