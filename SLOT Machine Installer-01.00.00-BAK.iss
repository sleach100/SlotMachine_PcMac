; ------------------------------------------------------------
; S.L.O.T. Machine — Windows Installer (Beta)
; Inno Setup 6.x — 64-bit only, Admin install, VST3 single-file (Windows)
; ------------------------------------------------------------


;  NOTE:  YOU MUST CHANGE THE CONTENTS OF "Version.txt" to match the app version you are deploying ***************** ************   SUPER IMPORTANT   *********** ************* ************** ********** **************
#define MyAppVersion  "01.00.00"   ; becomes SlotMachineSetup-01.00.26.exe
#define MySourceRoot  "C:\Users\Stever Leach\source\repos\Slot-Machine-V2\Builds\VisualStudio2022\x64\Release\Standalone Plugin"
; If your build outputs single-file VST3 inside the bundle folder, point to the actual file:
#define MyVST3File    "C:\Users\Stever Leach\source\repos\Slot-Machine-V2\Builds\VisualStudio2022\x64\Release\VST3\SlotMachine.vst3\Contents\x86_64-win\SlotMachine.vst3"

[Setup]
AppMutex=SlotMachineInstallerMutex
SetupIconFile="C:\Users\Stever Leach\source\repos\Slot-Machine-V2\SlotMachine.ico"
AppId={{A8B4A0C7-7C1C-4C14-8A9A-3B9D9C12E5F1}}
AppName=S.L.O.T. Machine
AppVersion={#MyAppVersion}
AppVerName=S.L.O.T. Machine {#MyAppVersion}
AppPublisher=Lone Pear Logic
AppPublisherURL=https://www.lonepearlogic.com
AppSupportURL=https://www.lonepearlogic.com/support
AppUpdatesURL=https://www.lonepearlogic.com/downloads
DefaultDirName={autopf}\SlotMachine
DefaultGroupName=SlotMachine
AllowNoIcons=yes
DisableDirPage=no
DisableProgramGroupPage=yes
OutputBaseFilename=SlotMachineSetup-{#MyAppVersion}
OutputDir=.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

; File-in-use handling
CloseApplications=yes
RestartApplications=no
UsePreviousLanguage=no
CloseApplicationsFilter=SlotMachine.exe;FL64.exe;Ableton*.exe;Cubase*.exe;Studio One*.exe;REAPER.exe;Bitwig*.exe;VST3PluginTestHost.exe;pluginval*.exe;WavesLocalServer.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &Desktop icon"; GroupDescription: "Additional icons:"; Flags: checkedonce
Name: "installvst3"; Description: "Install &VST3 beta plugin (64-bit)"; GroupDescription: "Components:"; Flags: checkedonce

; ---- PRE-DELETE to guarantee clean overwrite on reinstall ----
[InstallDelete]
; Wipe any old bundle-style plugin folder or stale file
Type: filesandordirs; Name: "{cf64}\VST3\SlotMachine.vst3"
; Ensure app folder contents get replaced cleanly (list specific files you ship)
Type: files; Name: "{app}\SlotMachine.exe"
Type: files; Name: "{app}\SlotMachineUpdater.exe"
Type: files; Name: "{app}\Version.txt"
Type: files; Name: "{app}\tutorialslotmachine.mp4"

[Files]
; --- Standalone app + assets ---
Source: "{#MySourceRoot}\SlotMachine.exe"; DestDir: "{app}"; \
  Flags: ignoreversion restartreplace

; --- Updater ---
Source: "{#MySourceRoot}\SlotMachineUpdater.exe"; DestDir: "{app}"; \
  Flags: ignoreversion restartreplace

; --- Version file used by the app ---
Source: "C:\Users\Stever Leach\source\repos\Slot-Machine-V2\Version.txt"; DestDir: "{app}"; \
  Flags: ignoreversion restartreplace

  ; --- Release Notes ---
Source: "C:\Users\Stever Leach\source\repos\Slot-Machine-V2\ReleaseNotes.txt"; DestDir: "{app}"; \
  Flags: ignoreversion restartreplace

; --- Optional tutorial/media ---
Source: "{#MySourceRoot}\tutorialslotmachine.mp4"; DestDir: "{app}"; \
  Flags: ignoreversion restartreplace

; --- VST3 single-file (Windows) ---
; Copy the built .vst3 file to Common Files\VST3 and overwrite no matter what.
Source: "{#MyVST3File}"; DestDir: "{cf64}\VST3"; DestName: "SlotMachine.vst3"; \
  Flags: ignoreversion restartreplace 64bit; Tasks: installvst3

[Icons]
Name: "{group}\S.L.O.T. Machine"; Filename: "{app}\SlotMachine.exe"
Name: "{commondesktop}\S.L.O.T. Machine"; Filename: "{app}\SlotMachine.exe"; Tasks: desktopicon

[Run]
; Shown only with UI, user can uncheck; skipped in silent because of 'postinstall'
Filename: "{app}\SlotMachine.exe"; Description: "Launch S.L.O.T. Machine"; Flags: postinstall nowait runasoriginaluser skipifsilent

; This one runs only when silent (no UI)
Filename: "{app}\SlotMachine.exe"; Flags: nowait runasoriginaluser skipifnotsilent


[Registry]
Root: HKLM; Subkey: "Software\Lone Pear Logic\SlotMachine"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Lone Pear Logic\SlotMachine"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"
Root: HKLM; Subkey: "Software\Lone Pear Logic\SlotMachine"; ValueType: string; ValueName: "MediaDir"; ValueData: "{app}"
