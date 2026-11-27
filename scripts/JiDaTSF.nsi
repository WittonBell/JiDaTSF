Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!ifndef PLATFORM
  !define PLATFORM "x64"
!endif
!ifndef STAGE_DIR
  !error "Pass STAGE_DIR when compiling the installer."
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "${__FILEDIR__}\..\dist\JiDaTSF-Setup-${PLATFORM}.exe"
!endif

!if "${PLATFORM}" == "x64"
  !define PROGRAM_FILES "$PROGRAMFILES64"
  !define REGISTRY_VIEW 64
  !define REGSVR32 "$WINDIR\Sysnative\regsvr32.exe"
!else
  !if "${PLATFORM}" == "x86"
    !define PROGRAM_FILES "$PROGRAMFILES32"
    !define REGISTRY_VIEW 32
    !define REGSVR32 "$SYSDIR\regsvr32.exe"
  !else
    !error "PLATFORM must be x86 or x64."
  !endif
!endif

!define PRODUCT_NAME "极达五笔"
!define PRODUCT_KEY "Software\JiDaTSF"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\JiDaTSF"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "${PROGRAM_FILES}\JiDaTSF"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "LegalCopyright" "Copyright (C) JiDaTSF"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} 安装程序"
VIAddVersionKey "FileVersion" "1.0.0.0"

!define MUI_ABORTWARNING
!define MUI_ICON "${__FILEDIR__}\..\res\app.ico"
!define MUI_UNICON "${__FILEDIR__}\..\res\app.ico"
!define MUI_WELCOMEPAGE_TITLE "${PRODUCT_NAME} 安装向导"
!define MUI_FINISHPAGE_TITLE "${PRODUCT_NAME} 已安装"
!define MUI_FINISHPAGE_TEXT "输入法组件已注册。请到 Windows 语言设置中添加“极达五笔”键盘。"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
  SetRegView ${REGISTRY_VIEW}
  ReadRegStr $0 HKLM "${PRODUCT_KEY}" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
!if "${PLATFORM}" == "x64"
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "x64 版本需要 64 位 Windows。"
    Abort
  ${EndIf}
!endif
FunctionEnd

Section "JiDaTSF" SEC_MAIN
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  File "${STAGE_DIR}\JiDaTSF.dll"

  SetOutPath "$INSTDIR\data"
  File "${STAGE_DIR}\data\jidian_wubi86.txt"
  File "${STAGE_DIR}\data\jidian_wubi86.txt.jdc"

  CreateDirectory "$INSTDIR\conf"
  ExecWait '"$SYSDIR\icacls.exe" "$INSTDIR\conf" /grant "*S-1-5-32-545:(OI)(CI)M" /T /C /Q' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "无法设置 conf 目录的用户配置写入权限。安装已中止（icacls 错误 $0）。"
    Abort
  ${EndIf}

  ExecWait '"${REGSVR32}" /s "$INSTDIR\JiDaTSF.dll"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "输入法注册失败（regsvr32 错误 $0）。请检查系统要求后重试。"
    Abort
  ${EndIf}

  WriteRegStr HKLM "${PRODUCT_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "1.0.0.0"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "JiDaTSF"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Function un.onInit
  SetRegView ${REGISTRY_VIEW}
FunctionEnd

Section "Uninstall"
  ExecWait '"${REGSVR32}" /s /u "$INSTDIR\JiDaTSF.dll"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "输入法注销失败（regsvr32 错误 $0），为避免留下无效注册信息，卸载已中止。"
    Abort
  ${EndIf}

  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "${PRODUCT_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\JiDaTSF.dll"
  Delete "$INSTDIR\data\jidian_wubi86.txt"
  Delete "$INSTDIR\data\jidian_wubi86.txt.jdc"
  RMDir "$INSTDIR\data"
  ; Keep conf and user settings so reinstalling or recovering them is possible.
  RMDir "$INSTDIR"
SectionEnd
