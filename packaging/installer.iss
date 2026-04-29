[Setup]
; 基本信息
; 运行方式：在项目根目录执行 iscc packaging\installer.iss
AppName=PIP-Link
AppVersion=2.1.0
AppPublisher=P1ne4pp1e
AppPublisherURL=https://github.com/P1ne4pp1e/PIP-Link
AppSupportURL=https://github.com/P1ne4pp1e/PIP-Link/issues
AppUpdatesURL=https://github.com/P1ne4pp1e/PIP-Link/releases
DefaultDirName={autopf}\PIP-Link
DefaultGroupName=PIP-Link
SourceDir=..
OutputDir=packaging\installer
OutputBaseFilename=PIP-Link-Setup-v2.1.0
SetupIconFile=assets\icon.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
DisableDirPage=no
DisableProgramGroupPage=no

; 许可协议
LicenseFile=LICENSE

; 64位支持
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
; Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 主程序 (--onedir 模式打包)
Source: "dist\PIP-Link\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; 额外文件
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
; 开始菜单
Name: "{group}\PIP-Link"; Filename: "{app}\PIP-Link.exe"
Name: "{group}\{cm:UninstallProgram,PIP-Link}"; Filename: "{uninstallexe}"
; 桌面图标
Name: "{autodesktop}\PIP-Link"; Filename: "{app}\PIP-Link.exe"; Tasks: desktopicon
; 快速启动
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\PIP-Link"; Filename: "{app}\PIP-Link.exe"; Tasks: quicklaunchicon

[Run]
; 安装完成后运行
Filename: "{app}\PIP-Link.exe"; Description: "{cm:LaunchProgram,PIP-Link}"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;
