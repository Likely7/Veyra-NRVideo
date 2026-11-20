#ifndef PayloadList
  #error Build this installer with scripts/package-windows-installer.py.
#endif

#define DisplayName "Veyra (" + Flavor + ")"
#define InstallId "Veyra.Win64." + Flavor

[Setup]
AppId={#InstallId}
AppName={#DisplayName}
AppVersion={#AppVersion}
AppPublisher=Veyra
AppPublisherURL=https://github.com/Likely7/Veyra-NRVideo
AppSupportURL=https://github.com/Likely7/Veyra-NRVideo/issues
AppUpdatesURL=https://github.com/Likely7/Veyra-NRVideo/releases
DefaultDirName={localappdata}\Programs\{#DisplayName}
DefaultGroupName={#DisplayName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupArchitecture=x64
MinVersion=10.0.19041
SetupMutex=Veyra.Windows.Setup
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=yes
UsePreviousTasks=yes
AllowRootDirectory=no
CloseApplications=no
RestartApplications=no
UninstallDisplayName={#DisplayName}
UninstallDisplayIcon={app}\Veyra.exe
UninstallFilesDir={app}
SetupIconFile={#AppIcon}
WizardStyle=modern dark hidebevels
WizardBackImageFile={#BrandBackground}
WizardBackColor=#100E1D
WizardImageFile={#BrandPanel}
WizardImageBackColor=#161425
WizardSmallImageFile={#BrandIcon}
WizardSmallImageBackColor=#161425
WizardSizePercent=118,112
Compression=lzma2/fast
SolidCompression=yes
LZMANumBlockThreads=1
OutputDir={#OutputDir}
OutputBaseFilename=Veyra-{#AppVersion}-{#Flavor}-win64-setup
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName=Veyra
VersionInfoDescription=Veyra Setup ({#Flavor})
VersionInfoCompany=Veyra
#ifdef PublisherSigning
SignTool=publisher
SignedUninstaller=yes
SignedUninstallerDir={#OutputDir}
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zhcn"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "zhtw"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
en.CloseVeyra=Close the Veyra instance in this folder and try again.
zhcn.CloseVeyra=请先退出此目录中的 Veyra，再重试。
en.SeparateFolder=Select a folder without another Veyra installation or portable copy.
zhcn.SeparateFolder=请选择未安装 Veyra 且没有便携版的目录。
en.AlreadyInstalled=This edition is already installed at %1. Use that location, or uninstall it before choosing another.
zhcn.AlreadyInstalled=此版本已安装在 %1。请使用原目录，或卸载后再更换安装位置。
en.UnsafeFolder=Select a local folder without symbolic links or junctions in its path.
zhcn.UnsafeFolder=请选择路径中没有符号链接或目录连接的本地文件夹。
en.VersionWriteFailed=Veyra could not save the active version. Check that the installation folder is writable.
zhcn.VersionWriteFailed=Veyra 无法保存当前版本。请检查安装目录是否可写。
en.InstalledVersion=The active installation already has a newer version of Veyra.
zhcn.InstalledVersion=当前安装目录已使用更新版本的 Veyra。
zhtw.CloseVeyra=請先結束此資料夾中的 Veyra，再試一次。
zhtw.SeparateFolder=請選擇沒有 Veyra 安裝或可攜版的資料夾。
zhtw.AlreadyInstalled=此版本已安裝在 %1。請使用原位置，或解除安裝後再選其他位置。
zhtw.UnsafeFolder=請選擇路徑中沒有符號連結或目錄接合點的本機資料夾。
zhtw.VersionWriteFailed=無法儲存目前的 Veyra 版本。請確認安裝資料夾可寫入。
zhtw.InstalledVersion=目前安裝的 Veyra 版本較新。
ja.CloseVeyra=このフォルダーの Veyra を終了してから、もう一度お試しください。
ja.SeparateFolder=別の Veyra やポータブル版が入っていないフォルダーを選んでください。
ja.AlreadyInstalled=このエディションは %1 にインストールされています。同じ場所を使うか、先にアンインストールしてください。
ja.UnsafeFolder=シンボリックリンクやジャンクションを含まないローカルフォルダーを選んでください。
ja.VersionWriteFailed=使用する Veyra のバージョンを保存できません。インストール先に書き込めるか確認してください。
ja.InstalledVersion=すでに新しいバージョンの Veyra が使われています。
ko.CloseVeyra=이 폴더의 Veyra를 종료한 뒤 다시 시도하세요.
ko.SeparateFolder=다른 Veyra 또는 포터블 버전이 없는 폴더를 선택하세요.
ko.AlreadyInstalled=이 에디션은 %1에 설치되어 있습니다. 같은 위치를 사용하거나 먼저 제거하세요.
ko.UnsafeFolder=심볼릭 링크 또는 정션이 없는 로컬 폴더를 선택하세요.
ko.VersionWriteFailed=활성 Veyra 버전을 저장할 수 없습니다. 설치 폴더에 쓰기 권한이 있는지 확인하세요.
ko.InstalledVersion=이미 더 새로운 Veyra 버전이 설치되어 있습니다.
es.CloseVeyra=Cierra la instancia de Veyra de esta carpeta y vuelve a intentarlo.
es.SeparateFolder=Elige una carpeta sin otra instalación ni una copia portátil de Veyra.
es.AlreadyInstalled=Esta edición ya está instalada en %1. Usa esa carpeta o desinstálala primero.
es.UnsafeFolder=Elige una carpeta local sin enlaces simbólicos ni uniones de directorio en la ruta.
es.VersionWriteFailed=No se pudo guardar la versión activa de Veyra. Comprueba que la carpeta permita escritura.
es.InstalledVersion=Ya hay una versión más reciente de Veyra activa.
fr.CloseVeyra=Fermez l’instance de Veyra de ce dossier, puis réessayez.
fr.SeparateFolder=Choisissez un dossier sans autre installation ni copie portable de Veyra.
fr.AlreadyInstalled=Cette édition est déjà installée dans %1. Utilisez ce dossier ou désinstallez-la d’abord.
fr.UnsafeFolder=Choisissez un dossier local sans lien symbolique ni jonction dans le chemin.
fr.VersionWriteFailed=Impossible d’enregistrer la version active de Veyra. Vérifiez les droits d’écriture du dossier.
fr.InstalledVersion=Une version plus récente de Veyra est déjà active.
de.CloseVeyra=Beenden Sie die Veyra-Instanz in diesem Ordner und versuchen Sie es erneut.
de.SeparateFolder=Wählen Sie einen Ordner ohne andere Veyra-Installation oder portable Kopie.
de.AlreadyInstalled=Diese Edition ist bereits unter %1 installiert. Verwenden Sie diesen Ordner oder deinstallieren Sie sie zuerst.
de.UnsafeFolder=Wählen Sie einen lokalen Ordner ohne symbolische Links oder Verzeichnisknoten im Pfad.
de.VersionWriteFailed=Die aktive Veyra-Version konnte nicht gespeichert werden. Prüfen Sie die Schreibrechte im Installationsordner.
de.InstalledVersion=Eine neuere Veyra-Version ist bereits aktiv.
ptbr.CloseVeyra=Feche a instância do Veyra nesta pasta e tente novamente.
ptbr.SeparateFolder=Selecione uma pasta sem outra instalação ou cópia portátil do Veyra.
ptbr.AlreadyInstalled=Esta edição já está instalada em %1. Use essa pasta ou desinstale a edição antes de mudar.
ptbr.UnsafeFolder=Selecione uma pasta local sem links simbólicos ou junções no caminho.
ptbr.VersionWriteFailed=Não foi possível salvar a versão ativa do Veyra. Verifique as permissões de gravação da pasta.
ptbr.InstalledVersion=Uma versão mais recente do Veyra já está ativa.
ru.CloseVeyra=Закройте Veyra из этой папки и попробуйте снова.
ru.SeparateFolder=Выберите папку без другой установки или переносной копии Veyra.
ru.AlreadyInstalled=Эта редакция уже установлена в %1. Используйте эту папку или сначала удалите программу.
ru.UnsafeFolder=Выберите локальную папку без символических ссылок и точек соединения в пути.
ru.VersionWriteFailed=Не удалось сохранить активную версию Veyra. Проверьте права на запись в папку установки.
ru.InstalledVersion=Уже используется более новая версия Veyra.

[Messages]
en.WelcomeLabel2=This will install Veyra {#AppVersion} for your Windows account.%n%nYou can choose an installation folder and whether to add a desktop shortcut.
zhcn.WelcomeLabel2=此向导将为当前 Windows 用户安装 Veyra {#AppVersion}。%n%n你可以选择安装位置，以及是否创建桌面快捷方式。
zhtw.WelcomeLabel2=此精靈將為目前的 Windows 使用者安裝 Veyra {#AppVersion}。%n%n你可以選擇安裝位置，以及是否建立桌面捷徑。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
#include PayloadList

[Icons]
Name: "{group}\Veyra"; Filename: "{app}\Veyra.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,Veyra}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#DisplayName}"; Filename: "{app}\Veyra.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Veyra.exe"; Description: "{cm:LaunchProgram,Veyra}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent unchecked

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#InstallId}_is1';
  ReparsePoint = $400;
  MoveReplaceExisting = $1;
  MoveWriteThrough = $8;
  InstalledMarker = 'veyra-installation.txt';
var
  PreviousVersion: String;
  ManagedInstall: Boolean;

function GetFileAttributes(Name: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function MoveFileEx(ExistingFile, NewFile: String; Flags: LongWord): Boolean;
  external 'MoveFileExW@kernel32.dll stdcall';

function LocalPath(const Path: String): String;
begin
  Result := Path;
  if Copy(Path, 1, 4) = '\\?\' then
    Delete(Result, 1, 4);
end;

function PlainPath(Path: String): Boolean;
var
  Parent: String;
  Attributes: LongWord;
begin
  Result := False;
  Path := LocalPath(Path);
  if Length(Path) < 3 then
    Exit;
  if (Path[2] <> ':') or (Path[3] <> '\') then
    Exit;
  repeat
    Attributes := GetFileAttributes(Path);
    if (Attributes <> $FFFFFFFF) and ((Attributes and ReparsePoint) <> 0) then
      Exit;
    Parent := ExtractFileDir(Path);
    if (Parent = '') or (CompareText(Parent, Path) = 0) then
      Break;
    Path := Parent;
  until False;
  Result := True;
end;

function SameFolder(Left, Right: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(LocalPath(Left)),
                        RemoveBackslashUnlessRoot(LocalPath(Right))) = 0;
end;

function HasOwnedMarker(const Root: String): Boolean;
var
  Data: AnsiString;
begin
  Result := LoadStringFromFile(AddBackslash(Root) + InstalledMarker, Data) and
            (Data = '{#InstallId}');
end;

function VeyraIsRunning(const Root: String): Boolean;
var
  LockFile: String;
  Data: AnsiString;
begin
  LockFile := AddBackslash(Root) + '.veyra-running.lock';
  Result := FileExists(LockFile) and not LoadStringFromFile(LockFile, Data);
end;

function DestinationError(const Root: String): String;
var
  Registered, Folder: String;
  OwnsFolder: Boolean;
begin
  Result := CustomMessage('UnsafeFolder');
  if not (PlainPath(Root) and PlainPath(AddBackslash(Root) + 'versions') and
     PlainPath(AddBackslash(Root) + 'versions\{#AppVersion}') and
     PlainPath(AddBackslash(Root) + '.veyra-update') and
     PlainPath(AddBackslash(Root) + 'current-version.txt') and
     PlainPath(AddBackslash(Root) + 'current-version.txt.new')) then
    Exit;
  OwnsFolder := RegQueryStringValue(HKCU64, UninstallKey, 'InstallLocation', Registered) and
                (Registered <> '') and DirExists(Registered);
  if OwnsFolder and not SameFolder(Root, Registered) then begin
    Result := FmtMessage(CustomMessage('AlreadyInstalled'), [Registered]);
    Exit;
  end;
  Folder := AddBackslash(Root);
  if (not OwnsFolder or not HasOwnedMarker(Root)) and
     (FileExists(Folder + 'Veyra.exe') or FileExists(Folder + 'veyra_qml_ui.exe') or
      FileExists(Folder + 'portable-package.txt') or FileExists(Folder + 'current-version.txt') or
      FileExists(Folder + InstalledMarker) or DirExists(Folder + 'versions') or
      DirExists(Folder + '.veyra-update')) then begin
    Result := CustomMessage('SeparateFolder');
    Exit;
  end;
  if VeyraIsRunning(Root) then begin
    Result := CustomMessage('CloseVeyra');
    Exit;
  end;
  Result := '';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    Exit;
  Problem := DestinationError(WizardDirValue);
  if Problem <> '' then begin
    SuppressibleMsgBox(Problem, mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function PreviousInstalledVersion(const Root: String; var FolderName: String): String;
var
  Data: AnsiString;
  FirstLineEnd, Dash, I: Integer;
  BuildNumber: Int64;
begin
  Result := '';
  FolderName := '';
  if not LoadStringFromFile(AddBackslash(Root) + 'current-version.txt', Data) then
    Exit;
  FirstLineEnd := Pos(#10, Data);
  if (FirstLineEnd <= 1) or (FirstLineEnd > 32) then
    Exit;
  FolderName := Copy(Data, 1, FirstLineEnd - 1);
  for I := 1 to Length(FolderName) do
    if Pos(FolderName[I], '0123456789.-abcdef') = 0 then begin
      FolderName := '';
      Exit;
    end;
  Dash := Pos('-', FolderName);
  if Dash = 0 then
    Result := FolderName
  else
    Result := Copy(FolderName, 1, Dash - 1);
  if not StrToVersion(Result + '.0', BuildNumber) or
     not FileExists(AddBackslash(Root) + 'versions\' + FolderName + '\veyra_qml_ui.exe') then begin
    Result := '';
    FolderName := '';
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  VersionText: String;
  Current, Incoming: Int64;
begin
  Result := DestinationError(WizardDirValue);
  if Result <> '' then
    Exit;
  VersionText := PreviousInstalledVersion(WizardDirValue, PreviousVersion);
  if (VersionText <> '') and StrToVersion(VersionText + '.0', Current) and
     StrToVersion('{#AppVersion}.0', Incoming) and (ComparePackedVersion(Current, Incoming) > 0) then
    Result := CustomMessage('InstalledVersion');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  StateFile, NewFile: String;
begin
  if CurStep <> ssPostInstall then
    Exit;
  if PreviousVersion = '{#AppVersion}' then
    Exit;
  StateFile := ExpandConstant('{app}\current-version.txt');
  NewFile := StateFile + '.new';
  if not SaveStringToFile(NewFile, '{#AppVersion}' + #10 + PreviousVersion + #10, False) or
     not MoveFileEx(NewFile, StateFile, MoveReplaceExisting or MoveWriteThrough) then
    RaiseException(CustomMessage('VersionWriteFailed'));
end;

function InitializeUninstall: Boolean;
var
  Root, Registered: String;
begin
  Root := LocalPath(ExpandConstant('{app}'));
  Result := PlainPath(Root) and PlainPath(Root + '\versions') and PlainPath(Root + '\versions\{#AppVersion}');
  if not Result then begin
    SuppressibleMsgBox(CustomMessage('UnsafeFolder'), mbError, MB_OK, IDOK);
    Exit;
  end;
  ManagedInstall := HasOwnedMarker(Root) and
    RegQueryStringValue(HKCU64, UninstallKey, 'InstallLocation', Registered) and
    SameFolder(Root, Registered);
  Result := not VeyraIsRunning(Root);
  if not Result then
    SuppressibleMsgBox(CustomMessage('CloseVeyra'), mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Root: String;
begin
  if (CurUninstallStep <> usPostUninstall) or not ManagedInstall then
    Exit;
  Root := LocalPath(ExpandConstant('{app}'));
  if not PlainPath(Root) then
    Exit;
  Log('Removing managed application versions and update cache.');
  DeleteFile(Root + '\current-version.txt');
  DeleteFile(Root + '\current-version.txt.new');
  DeleteFile(Root + '\.veyra-running.lock');
  DelTree(Root + '\versions', True, True, True);
  DelTree(Root + '\.veyra-update', True, True, True);
  RemoveDir(Root);
end;
