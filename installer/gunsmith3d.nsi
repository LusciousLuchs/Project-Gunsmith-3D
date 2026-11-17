; Gunsmith3D - the Windows installer (NSIS). Built by tools/make_installer.sh (and the release workflow) from the same
; files as the zip. It installs for the one Windows user, into their own programs folder, so no administrator prompt is
; needed and the game's self-updater can replace its files later. Players' own files (designs, maps, settings) live in
; Documents\OAB Foundry\Gunsmith and are never touched by installing, updating or uninstalling.
;
;   makensis -DVERSION=0.8.37-alpha -DVERNUM=0.8.37.0 -DSTAGE=<folder with the game files> -DOUT=<setup.exe> gunsmith3d.nsi

Target amd64-unicode   ; (the game is 64-bit only)
!include "MUI2.nsh"

!ifndef VERSION
  !error "VERSION is required"
!endif
!ifndef STAGE
  !error "STAGE (the folder of game files) is required"
!endif
!ifndef OUT
  !define OUT "Gunsmith3D-${VERSION}-setup.exe"
!endif

!define APP "Gunsmith3D"
!define PUBLISHER "OAB Foundry"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gunsmith3D"

Name "${APP}"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${PUBLISHER}"
VIProductVersion "${VERNUM}"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2026 The One Armed Bandit"

!define MUI_ICON "${STAGE}\gunsmith3d.ico"
!define MUI_UNICON "${STAGE}\gunsmith3d.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install ${APP} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This puts ${APP} on your computer with a Start menu entry.$\r$\n$\r$\nNo administrator rights are needed. The game keeps itself up to date after this.$\r$\n$\r$\nYour designs, maps and settings are kept in Documents\OAB Foundry\Gunsmith: installing, updating or uninstalling never touches them."
!define MUI_FINISHPAGE_RUN "$INSTDIR\gunsmith3d.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Play ${APP} now"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "${APP} (required)" SecGame
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetOverwrite on
  File "${STAGE}\gunsmith3d.exe"
  File "${STAGE}\gunsmith3d.ico"
  File "${STAGE}\README.md"
  File "${STAGE}\CHANGELOG.md"
  File "${STAGE}\BACKLOG.md"
  SetOutPath "$INSTDIR\licenses"
  File /r "${STAGE}\licenses\*.*"
  ; the sample guns, maps and textures: added if missing, never written over (like the updater does)
  SetOutPath "$INSTDIR\user"
  SetOverwrite off
  File /r "${STAGE}\user\*.*"
  SetOverwrite on
  SetOutPath "$INSTDIR"
  Delete "$INSTDIR\gunsmith3d.old.exe"

  CreateDirectory "$SMPROGRAMS\${PUBLISHER}"
  CreateShortcut "$SMPROGRAMS\${PUBLISHER}\${APP}.lnk" "$INSTDIR\gunsmith3d.exe" "" "$INSTDIR\gunsmith3d.ico"
  CreateShortcut "$SMPROGRAMS\${PUBLISHER}\Uninstall ${APP}.lnk" "$INSTDIR\uninstall.exe"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\gunsmith3d.ico"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" 30000
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\gunsmith3d.exe" "" "$INSTDIR\gunsmith3d.ico"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecGame} "The game itself, its sample guns and maps, and a Start menu entry."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut on your desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$INSTDIR\gunsmith3d.exe"
  Delete "$INSTDIR\gunsmith3d.old.exe"
  Delete "$INSTDIR\gunsmith3d.ico"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\CHANGELOG.md"
  Delete "$INSTDIR\BACKLOG.md"
  RMDir /r "$INSTDIR\licenses"
  ; the sample files that came with the game (listed when the installer was built); anything you added there stays
  !include "${STAGE}\uninstall_samples.nsh"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR\user"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${PUBLISHER}\${APP}.lnk"
  Delete "$SMPROGRAMS\${PUBLISHER}\Uninstall ${APP}.lnk"
  RMDir "$SMPROGRAMS\${PUBLISHER}"
  Delete "$DESKTOP\${APP}.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd
