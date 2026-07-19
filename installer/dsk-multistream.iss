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

[InstallDelete]
Type: filesandordirs; Name: "{app}\bin\64bit\tls"

[Files]
Source: "{#SourceRoot}\bin\64bit\obs-dsk-multistream.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SourceRoot}\data\locale\*"; DestDir: "{app}\data\locale"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\data\presets\*"; DestDir: "{app}\data\presets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\dsk-package-manifest.json"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
; This dedicated plugin directory contains binaries and static data only.
; Per-profile settings and Windows Credential Manager entries live elsewhere and are intentionally preserved.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"
Type: files; Name: "{app}\dsk-package-manifest.json"
Type: dirifempty; Name: "{app}"

[Code]
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
