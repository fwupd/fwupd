if get_option('efi_binary')
  efi_binary = dependency('fwupd-efi', required: false)
  if not efi_binary.found()
    subdir('efi')
  endif
endif

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
    'fu-uefi-vars.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    efivar,
    efiboot,
  ],
)

executable(
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
    'fu-uefi-vars.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  dependencies : [
    libxmlb,
    giounix,
    gusb,
    gudev,
    efivar,
    efiboot,
  ],
  link_with : [
    libfwupdprivate,
  ],
  install : true,
  install_dir : join_paths(libexecdir, 'fwupd'),
  c_args : cargs,
)

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
      'fu-ucs2.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      efivar,
      efiboot,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('uefi-self-test', e)
endif
