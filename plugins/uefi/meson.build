subdir('efi')

cargs = ['-DG_LOG_DOMAIN="FuPluginUefi"']

efi_os_dir = get_option('efi_os_dir')
if efi_os_dir != ''
  cargs += '-DEFI_OS_DIR="' + efi_os_dir + '"'
endif

install_data(['uefi.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_uefi',
  fu_hash,
  sources : [
    'fu-plugin-uefi.c',
    'fu-uefi-bgrt.c',
    'fu-ucs2.c',
    'fu-uefi-bootmgr.c',
    'fu-uefi-common.c',
    'fu-uefi-device.c',
    'fu-uefi-devpath.c',
    'fu-uefi-pcrs.c',
    'fu-uefi-update-info.c',
    'fu-uefi-udisks.c',
    'fu-uefi-vars.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    efivar,
    efiboot,
    tpm2tss,
  ],
)

fwupdate = executable(
  'fwupdate',
  resources_src,
  fu_hash,
  sources : [
    'fu-uefi-tool.c',
    'fu-uefi-bgrt.c',
    'fu-ucs2.c',
    'fu-uefi-bootmgr.c',
    'fu-uefi-common.c',
    'fu-uefi-device.c',
    'fu-uefi-devpath.c',
    'fu-uefi-pcrs.c',
    'fu-uefi-update-info.c',
    'fu-uefi-udisks.c',
    'fu-uefi-vars.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    libxmlb,
    giounix,
    gusb,
    gudev,
    efivar,
    efiboot,
    tpm2tss,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  install : true,
  install_dir : bindir,
  c_args : cargs,
)

if get_option('man')
  custom_target('fwupdate-man',
    input : fwupdate,
    output : 'fwupdate.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'Debugging utility for UEFI firmware updates',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
endif

install_data(['uefi.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'uefi-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-uefi-bgrt.c',
      'fu-uefi-bootmgr.c',
      'fu-uefi-common.c',
      'fu-uefi-device.c',
      'fu-uefi-devpath.c',
      'fu-uefi-pcrs.c',
      'fu-uefi-update-info.c',
      'fu-uefi-vars.c',
      'fu-uefi-udisks.c',
      'fu-ucs2.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      efivar,
      efiboot,
      tpm2tss,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  test('uefi-self-test', e)
endif
