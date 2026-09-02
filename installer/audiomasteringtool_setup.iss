; Script generated for AudioMasteringTool Inno Setup Installer
#define MyAppName "AudioMasteringTool"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "DocDamage"
#define MyAppURL "https://github.com/DocDamage/audiomasteringtool"
#define MyAppExeName "audiomasteringtool.exe"

[Setup]
AppId={{D37E84C1-28B9-4C41-8408-A97321F55C9E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=auto
OutputDir=..\dist
OutputBaseFilename=AudioMasteringTool_Setup_1.0.0_win64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64compatible
ChangesAssociations=no
PrivilegesRequired=lowest
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\AudioMasteringTool-1.0.0-win64\*"; DestDir: "{app}"; Excludes: "SHA256SUMS.txt"; Flags: ignoreversion recursesubdirs createallsubdirs

; The packaged model registry intentionally lives beside the executable at
; {app}\models\registry.json. Downloaded model weights are written on demand to
; %LOCALAPPDATA% by the application and are deliberately not removed on uninstall.

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\AudioMasteringTool CLI"; Filename: "{app}\amt_cli.exe"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
