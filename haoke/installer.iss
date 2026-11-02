; Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
; Independent per-user Haoke VLC installation. GPL-2.0-or-later.
; Build parameters and all public version identifiers come from build_setup.py.
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef AssetsDir
  #error AssetsDir is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef FileVersion
  #error FileVersion is required
#endif
#ifndef OutputName
  #error OutputName is required
#endif
#ifndef MaxAppPath
  #error MaxAppPath is required
#endif
#ifndef SignedDir
  #error SignedDir is required
#endif

[Setup]
AppId=Haoke.VLC.b92689d5ae8d3982.Windows
AppName={cm:AppName}
AppVersion={#AppVersion}
AppVerName={cm:AppName} {#AppVersion}
AppPublisher=Haoke Tools (VLC Optimized Edition)
AppPublisherURL=https://haoke.org
AppSupportURL=https://haoke.org/software/b92689d5ae8d3982
DefaultDirName={localappdata}\Programs\HaokeVLC
DefaultGroupName=Haoke Tools
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
WizardStyle=modern
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=yes
AllowNoIcons=yes
UsePreviousAppDir=yes
UsePreviousLanguage=no
LanguageDetectionMethod=none
ShowLanguageDialog=no
CloseApplications=yes
CloseApplicationsFilter=vlc.exe,libqt_plugin.dll
RestartApplications=no
UninstallDisplayIcon={app}\vlc.exe
UninstallDisplayName={cm:AppName}
SetupIconFile={#AssetsDir}\vlc.ico
VersionInfoVersion={#FileVersion}
VersionInfoProductVersion={#FileVersion}
VersionInfoProductName=VLC media player (Haoke)
VersionInfoDescription=VLC media player - Haoke setup
VersionInfoCompany=Haoke Tools (VLC Optimized Edition)
VersionInfoOriginalFileName=Setup.exe
OutputBaseFilename={#OutputName}
Compression=lzma2/normal
SolidCompression=yes
DiskSpanning=no
SignedUninstaller=yes
SignedUninstallerDir={#SignedDir}
SignTool=haoke_vlc_setup
SignToolRetryCount=0
DisableReadyPage=no
ChangesAssociations=no
ChangesEnvironment=no
LicenseFile={#PayloadDir}\COPYING.txt

[Languages]
Name: "en_US"; MessagesFile: "compiler:Default.isl"
Name: "zh_CN"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[LangOptions]
DialogFontName=Noto Sans CJK SC
DialogFontSize=9
WelcomeFontName=Noto Sans CJK SC

[CustomMessages]
en_US.AppName=VLC media player (Haoke)
zh_CN.AppName=全能影音播放器
en_US.DesktopShortcut=Create a desktop shortcut
zh_CN.DesktopShortcut=创建桌面快捷方式
en_US.LaunchApp=Launch %1
zh_CN.LaunchApp=启动 %1
en_US.BadDirectory=Choose an empty, writable local folder for this independent Haoke installation. Windows, Program Files, shared system folders, reparse-point paths, and folders belonging to other applications cannot be used.
zh_CN.BadDirectory=请选择空的、当前用户可写的本地文件夹安装浩科版。不能使用 Windows、Program Files、系统共享目录、包含链接的路径或其他软件的文件夹。
en_US.PathTooLong=The selected installation path is too long. Choose a shorter local path so the player and its isolated profile remain within Windows path limits.
zh_CN.PathTooLong=所选安装路径过长。请选择较短的本地路径，确保播放器及隔离配置符合 Windows 路径限制。
en_US.NotWritable=The selected folder is not writable by the current user. Choose another folder; this installer does not request administrator elevation.
zh_CN.NotWritable=当前用户无法写入所选文件夹。请选择其他目录；此安装器不会请求管理员提权。
en_US.ShortcutKept=An existing shortcut belongs to another application and has been preserved. You can launch this Haoke edition from the chosen installation folder.
zh_CN.ShortcutKept=已有同名快捷方式属于其他程序，已予以保留。可从所选安装目录启动本浩科版。

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[Files]
; A private process font is extracted before the first wizard form is created.
Source: "{#AssetsDir}\NotoSansCJKsc-Regular.otf"; Flags: dontcopy noencryption
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "portable\*"
Source: "{#AssetsDir}\NotoSansCJKsc-Regular.otf"; DestDir: "{app}\haoke-fonts"; Flags: ignoreversion
Source: "{#AssetsDir}\LICENSE.txt"; DestDir: "{app}\haoke-fonts"; Flags: ignoreversion
Source: "{#AssetsDir}\source.json"; DestDir: "{app}\haoke-fonts"; Flags: ignoreversion
Source: "{#AssetsDir}\haoke-install.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Dirs]
; Config, Qt settings and signed-update state remain user-owned after uninstall.
Name: "{app}\portable"; Flags: uninsneveruninstall

[Icons]
Name: "{userprograms}\Haoke Tools\{cm:AppName}"; Filename: "{app}\vlc.exe"; WorkingDir: "{app}"; Check: CanCreateStartShortcut
Name: "{userdesktop}\{cm:AppName}"; Filename: "{app}\vlc.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Check: CanCreateDesktopShortcut

[Run]
Filename: "{app}\vlc.exe"; Description: "{cm:LaunchApp,{cm:AppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent unchecked

[Code]
const
  ProductMarker = 'Haoke.VLC.b92689d5ae8d3982.Windows';
  FileAttributeReparsePoint = $400;
  InvalidFileAttributes = $FFFFFFFF;
  PrivateFont = $10;

var
  PrivateFontFile: String;
  UninstallFontFile: String;
  PreservedForeignShortcut: Boolean;

function GetFileAttributesW(Name: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function WinGetTempFileName(Directory, Prefix: String; Unique: LongWord; Output: String): LongWord;
  external 'GetTempFileNameW@kernel32.dll stdcall';
function AddFontResourceEx(FileName: String; Flags: LongWord; Reserved: Integer): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall';
function RemoveFontResourceEx(FileName: String; Flags: LongWord; Reserved: Integer): Boolean;
  external 'RemoveFontResourceExW@gdi32.dll stdcall';

function NormalPath(Value: String): String;
begin
  Result := Lowercase(RemoveBackslashUnlessRoot(ExpandFileName(Value)));
end;

function IsInside(Value, Parent: String): Boolean;
begin
  Value := NormalPath(Value);
  Parent := NormalPath(Parent);
  Result := (Value = Parent) or (Pos(AddBackslash(Parent), AddBackslash(Value)) = 1);
end;

function HasReparseAncestor(Value: String): Boolean;
var
  Attr: LongWord;
  Parent: String;
begin
  Result := False;
  Value := RemoveBackslashUnlessRoot(ExpandFileName(Value));
  while Length(Value) > 2 do begin
    Attr := GetFileAttributesW(Value);
    if (Attr <> InvalidFileAttributes) and ((Attr and FileAttributeReparsePoint) <> 0) then begin
      Result := True;
      Exit;
    end;
    Parent := RemoveBackslashUnlessRoot(ExtractFileDir(Value));
    if Parent = Value then Exit;
    Value := Parent;
  end;
end;

function DirectoryHasEntries(Value: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Value) + '*', FindRec) then begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then begin
          Result := True;
          Break;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

function IsOurDataDirectory(Value: String): Boolean;
begin
  Result := GetIniString('HaokeInstall', 'ProductId', '', AddBackslash(Value) + 'haoke-install.ini') = ProductMarker;
end;

function CurrentUserCanWrite(Value: String): Boolean;
var
  Existing, Parent, Probe: String;
begin
  Result := False;
  Existing := Value;
  while not DirExists(Existing) do begin
    Parent := ExtractFileDir(Existing);
    if Parent = Existing then Exit;
    Existing := Parent;
  end;
  SetLength(Probe, 260);
  if WinGetTempFileName(Existing, 'hkv', 0, Probe) = 0 then Exit;
  Probe := Copy(Probe, 1, Pos(#0, Probe) - 1);
  Result := DeleteFile(Probe);
end;

function ValidateDirectory(Value: String): String;
begin
  Result := '';
  Value := RemoveBackslashUnlessRoot(ExpandFileName(Value));
  if Length(Value) > {#MaxAppPath} then begin
    Result := CustomMessage('PathTooLong');
    Exit;
  end;
  if (Length(Value) < 4) or (Copy(Value, 2, 2) <> ':\') or
     IsInside(Value, ExpandConstant('{win}')) or
     IsInside(Value, ExpandConstant('{pf32}')) or
     IsInside(Value, ExpandConstant('{pf64}')) or
     IsInside(Value, ExpandConstant('{commonappdata}')) or
     IsInside(Value, ExpandConstant('{commonprograms}')) or
     IsInside(Value, ExpandConstant('{commondesktop}')) or
     HasReparseAncestor(Value) or
     HasReparseAncestor(AddBackslash(Value) + 'portable\vlc') then begin
    Result := CustomMessage('BadDirectory');
    Exit;
  end;
  if DirExists(Value) and DirectoryHasEntries(Value) and not IsOurDataDirectory(Value) then begin
    Result := CustomMessage('BadDirectory');
    Exit;
  end;
  if not CurrentUserCanWrite(Value) or
     not CurrentUserCanWrite(AddBackslash(Value) + 'portable\vlc') then
    Result := CustomMessage('NotWritable');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Error: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    Error := ValidateDirectory(WizardDirValue);
    if Error <> '' then begin
      MsgBox(Error, mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := ValidateDirectory(WizardDirValue);
end;

procedure RemoveOwnedShortcut(FileName: String);
var
  Shell, Shortcut: Variant;
begin
  if not FileExists(FileName) then Exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    Shortcut := Shell.CreateShortcut(FileName);
    if NormalPath(Shortcut.TargetPath) = NormalPath(ExpandConstant('{app}\vlc.exe')) then
      DeleteFile(FileName);
  except
    Log('Could not inspect a prior shortcut; preserved it.');
  end;
end;

function CanUseShortcutSlot(FileName: String): Boolean;
var
  Shell, Shortcut: Variant;
begin
  Result := True;
  if not FileExists(FileName) then Exit;
  Result := False;
  try
    Shell := CreateOleObject('WScript.Shell');
    Shortcut := Shell.CreateShortcut(FileName);
    Result := NormalPath(Shortcut.TargetPath) = NormalPath(ExpandConstant('{app}\vlc.exe'));
  except
    Result := False;
  end;
  if not Result then begin
    PreservedForeignShortcut := True;
    Log('Preserved a shortcut slot belonging to another application: ' + FileName);
  end;
end;

function CanCreateStartShortcut: Boolean;
begin
  Result := CanUseShortcutSlot(ExpandConstant('{userprograms}\Haoke Tools\{cm:AppName}.lnk'));
end;

function CanCreateDesktopShortcut: Boolean;
begin
  Result := CanUseShortcutSlot(ExpandConstant('{userdesktop}\{cm:AppName}.lnk'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ProfileDir, ConfigFile: String;
begin
  if CurStep = ssInstall then begin
    RemoveOwnedShortcut(ExpandConstant('{userprograms}\Haoke Tools\全能影音播放器.lnk'));
    RemoveOwnedShortcut(ExpandConstant('{userprograms}\Haoke Tools\VLC media player (Haoke).lnk'));
    RemoveOwnedShortcut(ExpandConstant('{userdesktop}\全能影音播放器.lnk'));
    RemoveOwnedShortcut(ExpandConstant('{userdesktop}\VLC media player (Haoke).lnk'));
  end;
  if CurStep = ssPostInstall then begin
    ProfileDir := ExpandConstant('{app}\portable\vlc');
    ConfigFile := AddBackslash(ProfileDir) + 'vlcrc';
    if not FileExists(ConfigFile) then begin
      if not ForceDirectories(ProfileDir) or
         not SaveStringToFile(ConfigFile,
           '[qt]'#13#10'qt-privacy-ask=0'#13#10'qt-updates-notif=0'#13#10 +
           '[core]'#13#10'metadata-network-access=0'#13#10, False) then
        RaiseException(CustomMessage('NotWritable'));
    end;
    if PreservedForeignShortcut and not WizardSilent then
      MsgBox(CustomMessage('ShortcutKept'), mbInformation, MB_OK);
  end;
end;

function InitializeSetup: Boolean;
begin
  ExtractTemporaryFile('NotoSansCJKsc-Regular.otf');
  PrivateFontFile := ExpandConstant('{tmp}\NotoSansCJKsc-Regular.otf');
  Result := AddFontResourceEx(PrivateFontFile, PrivateFont, 0) > 0;
end;

procedure DeinitializeSetup;
begin
  if PrivateFontFile <> '' then RemoveFontResourceEx(PrivateFontFile, PrivateFont, 0);
end;

function InitializeUninstall: Boolean;
var
  Buffer: String;
begin
  Result := True;
  SetLength(Buffer, 260);
  if WinGetTempFileName(ExpandConstant('{tmp}'), 'hkv', 0, Buffer) = 0 then Exit;
  UninstallFontFile := Copy(Buffer, 1, Pos(#0, Buffer) - 1);
  if CopyFile(ExpandConstant('{app}\haoke-fonts\NotoSansCJKsc-Regular.otf'), UninstallFontFile, False) then
    AddFontResourceEx(UninstallFontFile, PrivateFont, 0);
end;

procedure DeinitializeUninstall;
begin
  if UninstallFontFile <> '' then begin
    RemoveFontResourceEx(UninstallFontFile, PrivateFont, 0);
    DeleteFile(UninstallFontFile);
  end;
end;
