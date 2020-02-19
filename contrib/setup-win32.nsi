#!Nsis Installer Command Script
#
# To build an installer from the script you would normally do:
#
#   dnf install mingw32-nsis
#   makensis setup-win32.nsi

Name ""
OutFile "setup/fwupd-@FWUPD_VERSION@-setup-x86_64.exe"
InstallDir "$ProgramFiles\fwupd"
InstallDirRegKey HKLM SOFTWARE\fwupd "Install_Dir"
ShowInstDetails hide
ShowUninstDetails hide
XPStyle on
Page directory
Page instfiles

ComponentText "Select which optional components you want to install."

DirText "Please select the installation folder."

Section "fwupd"
  SectionIn RO

  SetOutPath "$INSTDIR\bin"

  # deps
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/iconv.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libarchive-13.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libbrotlicommon.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libbrotlidec.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libbz2-1.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libffi-6.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgcc_s_seh-1.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgio-2.0-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libglib-2.0-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgmodule-2.0-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgmp-10.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgnutls-30.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgnutls-30.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgobject-2.0-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgusb-2.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libhogweed-4.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libidn2-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libintl-8.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libjson-glib-1.0-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/liblzma-5.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libnettle-6.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libp11-kit-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libpcre-1.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libpsl-5.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libsoup-2.4-1.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libsqlite3-0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libtasn1-6.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libunistring-2.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libusb-1.0.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libxml2-2.dll"
  File "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/zlib1.dll"

  # fwupd
  File "dfu-tool.exe"
  File "fwupdtool.exe"
  File "libfwupd-2.dll"
  File "libfwupdplugin-1.dll"
  File "libgcab-1.0-0.dll"
  File "libxmlb-1.dll"
  SetOutPath "$INSTDIR\fwupd-plugins-3"
  File /r "fwupd-plugins-3/libfu_plugin_*.dll"
  SetOutPath "$INSTDIR\etc\fwupd"
  File "etc/fwupd/daemon.conf"
  SetOutPath "$INSTDIR\etc\pki\fwupd"
  File "etc/pki/fwupd/LVFS-CA.pem"
  SetOutPath "$INSTDIR\share\fwupd\quirks.d"
  File /r "share/fwupd/quirks.d/*.quirk"

  ReadEnvStr $0 COMSPEC
  SetOutPath "$INSTDIR"
SectionEnd

Section "Uninstall"
  RMDir /rebootok /r "$SMPROGRAMS\fwupd"
  RMDir /rebootok /r "$INSTDIR\bin"
  RMDir /rebootok /r "$INSTDIR\etc"
  RMDir /rebootok /r "$INSTDIR\lib"
  RMDir /rebootok /r "$INSTDIR\share"
  RMDir /rebootok "$INSTDIR"
SectionEnd

Section -post
  WriteUninstaller "$INSTDIR\Uninstall fwupd.exe"
SectionEnd
