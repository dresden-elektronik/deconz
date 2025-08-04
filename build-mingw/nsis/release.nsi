;NSIS Modern User Interface
;Start Menu Folder Selection Example Script
;Originally written by Joost Verburg

;Modified and adapted by ele@de

; ----------------------------------------------------------------------------------
; ********************** STEPS IF SOMETHING CHANGED ********************************
; ----------------------------------------------------------------------------------
; 1.) Set SRC_DIR - source directory where all your files are located
; 2.) Set DEST_DIR - destination directory where you want finally place the application
; 3.) Set DEST_NAME - the final name of your application
; 4.) Set HEADER_PATH - path to header, which is shown on install and uninstall dialog
; 5.) Set HEADER_NAME- name of header, which is shown on install and uninstall dialog
; 6.) Set LICENSE_PATH - Path to license
; 7.) Set LICENSE_NAME - Name of your license
; 8.) Set all files which should be within setup file @see 'SECTION FOR INSTALLING'
; 9.) Set all files which should be uninstalled when execute Uninstall.exe @see 'SECTION FOR UNINSTALLING'

; ----------------------------------------------------------------------------------
; **************************** GLOBAL PARAMETERS ***********************************
; ----------------------------------------------------------------------------------

  SetCompressor /SOLID LZMA

  ; Set Source directory, where install files are located
  !define SRC_DIR   ".."

  ; Define Destination directory
  !define DEST_DIR  "deCONZ"

  ; Define Destination name 
  !define DEST_NAME "deCONZ"
  
  ; Define Destination name 
  !define FILE_VERSION "V2_05"

  ; Define Destination name 
  !define DISPL_NAME "deCONZ"
  
  ; Define Destination name 
  !define APP_VERSION "2.05.69.0"
  
  ; Define Header path which is shown at install/uninstall
  !define HEADER_PATH    "images"
  
  ; Define Header name which is shown at install/uninstall (must be an BMP)
  !define HEADER_NAME    "de_header_left_center.bmp"
  
  ; Define path to license
  !define LICENSE_PATH  "."
  
  ; Define name of license
  !define LICENSE_NAME  "license.txt"

  !define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\deCONZ"

  !define MULTIUSER_EXECUTIONLEVEL Highest
  !define MULTIUSER_MUI
  !define MULTIUSER_INSTALLMODE_COMMANDLINE
  ; !define MULTIUSER_INSTALLMODE_DEFAULT_CURRENTUSER
  !define MULTIUSER_INSTALLMODE_INSTDIR "${DEST_NAME}"
  !define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY  "Software\${DEST_NAME}"

; ----------------------------------------------------------------------------------
; ******************************** INCLUDES ****************************************
; ----------------------------------------------------------------------------------
  !include "MultiUser.nsh"
  !include "MUI2.nsh"
  !include "FileFunc.nsh"

; ----------------------------------------------------------------------------------
; ***************************** SETUP PARAMETERS ***********************************
; ----------------------------------------------------------------------------------

  

  ;Name and file
  Name "${DISPL_NAME}"
  OutFile "deCONZ_Setup_Win32_${FILE_VERSION}.exe"
  
  ; Set the default Installation Directory
  ;InstallDir "$LOCALAPPDATA\${DEST_NAME}"
  
  ;Get installation folder from registry if available
  ;InstallDirRegKey HKLM "Software\${DEST_NAME}" ""

  ;Request application privileges for Windows Vista
  RequestExecutionLevel user

  VIProductVersion ${APP_Version}
  VIAddVersionKey "ProductName" "deCONZ"

  VIAddVersionKey "CompanyName" "dresden elektronik ingenieurtechnik gmbh"
  VIAddVersionKey "LegalTrademarks" "deCONZ is a trademark of dresden elektronik ingenieurtechnik gmbh"
  VIAddVersionKey "LegalCopyright" "© dresden elektronik ingenieurtechnik gmbh"
  VIAddVersionKey "FileDescription" "deCONZ"
  VIAddVersionKey "FileVersion" "${APP_VERSION}"

; ----------------------------------------------------------------------------------
; ********************************* VARIABLES **************************************
; ----------------------------------------------------------------------------------

  Var StartMenuFolder


; ----------------------------------------------------------------------------------
; **************************** INTERFACE SETTINGS **********************************
; ----------------------------------------------------------------------------------

  !define MUI_ABORTWARNING
  !define MUI_HEADERIMAGE
  !define MUI_HEADERIMAGE_BITMAP "${HEADER_PATH}\${HEADER_NAME}"
  

; ----------------------------------------------------------------------------------
; *********************************** PAGES ****************************************
; ----------------------------------------------------------------------------------
;  !insertmacro MULTIUSER_PAGE_INSTALLMODE
  !insertmacro MUI_PAGE_LICENSE "${LICENSE_PATH}\${LICENSE_NAME}"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
    
  ;Start Menu Folder Page Configuration
  !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM" 
  !define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\${DISPL_NAME}" 
  !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
  
  !insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
  
  !insertmacro MUI_PAGE_INSTFILES
  
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES


; ----------------------------------------------------------------------------------
; ********************************* LANGUAGES **************************************
; ----------------------------------------------------------------------------------
 
  !insertmacro MUI_LANGUAGE "English"

; ----------------------------------------------------------------------------------
; *************************** SECTION FOR FUNCTIONS ********************************
; ----------------------------------------------------------------------------------
Function .onInit
  !insertmacro MULTIUSER_INIT
FunctionEnd



; ----------------------------------------------------------------------------------
; *************************** SECTION FOR INSTALLING *******************************
; ----------------------------------------------------------------------------------

Section "${DISPL_NAME}" SecApp
  SetOutPath "$INSTDIR"
  
  IfFileExists "$INSTDIR\bin" 0 +1
  RMDir /r $INSTDIR\bin
  IfFileExists "$INSTDIR\devices" 0 +1
  RMDir /r $INSTDIR\devices

  ; all the files which should be within setup file
  File /r ${SRC_DIR}\bin ; /r - copy root directory with all subdirectories and files
  File /r ${SRC_DIR}\zcl
  ;File /r ${SRC_DIR}\images
  File /r ${SRC_DIR}\icons
  File /r ${SRC_DIR}\doc
  File /r ${SRC_DIR}\plugins
  ;File /r ${SRC_DIR}\firmware
  ;File /r ${SRC_DIR}\otau
  File /r ${SRC_DIR}\devices

  ; write the zcldb initial file
  SetOutPath "$LOCALAPPDATA\dresden-elektronik\${DEST_NAME}"
  FileOpen $4 "zcldb.txt" w
  FileWrite $4 "$INSTDIR\zcl\general.xml$\r$\n"
  FileWrite $4 "$INSTDIR\zcl\bitcloud.xml"
  FileWrite $4 "$\r$\n" ; we write an extra line
  FileClose $4 ; and close the file
  
  ; devices local user dir
  IfFileExists "$LOCALAPPDATA\dresden-elektronik\deCONZ\devices" +1 0
  CreateDirectory "$LOCALAPPDATA\dresden-elektronik\deCONZ\devices"

  ; copy firmware
  IfFileExists "$LOCALAPPDATA\dresden-elektronik\deCONZ\firmware" +2 0
  CreateDirectory "$LOCALAPPDATA\dresden-elektronik\deCONZ\firmware"
  CreateDirectory "$LOCALAPPDATA\dresden-elektronik\deCONZ\otau"

  CopyFiles "$INSTDIR\firmware\*" "$LOCALAPPDATA\dresden-elektronik\deCONZ\firmware"
  CopyFiles "$INSTDIR\otau\*" "$LOCALAPPDATA\dresden-elektronik\deCONZ\otau"
  
  RMDir /R "$INSTDIR\build"

  ;Cleanup legacy installation folder
  DeleteRegKey HKCU "Software\${DEST_NAME}"    
  DeleteRegKey SHCTX "Software\${DEST_NAME}"

  ;Store installation folder
  WriteRegStr HKLM "Software\${DEST_NAME}" "" $INSTDIR
  
  ;Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ;Cleanup legacy uninstaller
  DeleteRegKey HKCU  "${UNINST_KEY}"
  DeleteRegKey SHCTX  "${UNINST_KEY}"

  ;Add uninstall information to Add/Remove Programs
 
  WriteRegStr HKLM  "${UNINST_KEY}" "DisplayName" "${DISPL_NAME}"
  WriteRegStr HKLM  "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM  "${UNINST_KEY}" "Publisher" "dresden elektronik ingenieurtechnik gmbh"
  WriteRegStr HKLM  "${UNINST_KEY}" "URLUpdateInfo" "http://www.dresden-elektronik.de"
  WriteRegStr HKLM  "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM  "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\bin\deCONZ.exe"
  WriteRegStr HKLM  "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM  "${UNINST_KEY}" "EstimatedSize" "$0"
  
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    
    ;Create shortcuts to startmenu
    SetOutPath $INSTDIR ; for working directory
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\${DISPL_NAME}.lnk" "$INSTDIR\bin\deCONZ.exe"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\User Manual.lnk" "$INSTDIR\doc\deCONZ-BHB-en.pdf"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "control.exe" "appwiz.cpl"
  
  !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

  ;--------------------------------
  ; Installer Descriptions

  ;Language strings
  LangString DESC_App ${LANG_ENGLISH} "Install ${DISPL_NAME} ${APP_VERSION}."

  ;Assign language strings to sections
  !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_App)
  !insertmacro MUI_FUNCTION_DESCRIPTION_END
 
; ----------------------------------------------------------------------------------
; ************************** SECTION FOR UNINSTALLING ******************************
; ----------------------------------------------------------------------------------

Section "Uninstall"

  ; remove all the files and folders
  RMDir /r $INSTDIR\bin ; /r - remove root directory with all subdirectories and files
  ;RMDir /r $INSTDIR\images
  RMDir /r $INSTDIR\icons

  Delete "$INSTDIR\Uninstall.exe"

  RMDir /r "$INSTDIR"
  
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    
  ; remove shortcuts of startmenu  
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\User Manual.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\deCONZ.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"

  ; remove legacy registry keys
  DeleteRegKey SHCTX  "${UNINST_KEY}"
  DeleteRegKey SHCTX "Software\${DEST_NAME}"

  ; remove registry keys
  DeleteRegKey /ifempty HKLM  "${UNINST_KEY}"
  DeleteRegKey /ifempty HKLM "Software\${DEST_NAME}"

SectionEnd

;--------------------------------
;Uninstaller Functions
Function un.onInit
  !insertmacro MULTIUSER_UNINIT
FunctionEnd