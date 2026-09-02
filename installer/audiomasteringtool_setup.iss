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
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=auto
OutputDir=..\dist
OutputBaseFilename=AudioMasteringTool_Setup_1.0.0_win64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\win\src\app\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\win\src\worker\Release\amt_worker.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\win\src\cli\Release\amt_cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\models\*"; DestDir: "{localappdata}\AudioMasteringTool\models"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\AudioMasteringTool CLI"; Filename: "{app}\amt_cli.exe"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.amtproj"; ValueType: string; ValueName: ""; ValueData: "AudioMasteringTool.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\AudioMasteringTool.Project"; ValueType: string; ValueName: ""; ValueData: "AudioMasteringTool Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\AudioMasteringTool.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\AudioMasteringTool.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
