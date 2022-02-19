if efiboot.found() and efivar.found()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiCapsule"']

efi_os_dir = get_option('efi_os_dir')
if efi_os_dir != ''
  cargs += '-DEFI_OS_DIR="' + efi_os_dir + '"'
endif

install_data(['uefi-capsule.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d'))

backend_srcs = ['fu-uefi-backend.c']
if host_machine.system() == 'linux'
  backend_srcs += 'fu-uefi-backend-linux.c'
  # replace @localstatedir@
  con2 = configuration_data()
  con2.set('localstatedir', localstatedir)
  configure_file(
    input : 'fwupd.grub.conf.in',
    output : '35_fwupd',
    configuration : con2,
    install: true,
    install_dir: join_paths(sysconfdir, 'grub.d')
  )
elif host_machine.system() == 'freebsd'
  backend_srcs += 'fu-uefi-backend-freebsd.c'
else
  error('no ESRT support for @0@'.format(host_machine.system()))
endif

shared_module('fu_plugin_uefi_capsule',
  fu_hash,
  sources : [
    'fu-plugin-uefi-capsule.c',
    'fu-uefi-bgrt.c',
    'fu-ucs2.c',
    'fu-uefi-bootmgr.c',
    'fu-uefi-common.c',
    'fu-uefi-cod-device.c',
    'fu-uefi-nvram-device.c',
    'fu-uefi-grub-device.c',
    'fu-uefi-device.c',
    'fu-uefi-devpath.c',
    'fu-uefi-update-info.c',
    backend_srcs,
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
    platform_deps,
    efiboot,
  ],
)

if get_option('compat_cli')
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
    'fu-uefi-cod-device.c',
    'fu-uefi-nvram-device.c',
    'fu-uefi-grub-device.c',
    'fu-uefi-devpath.c',
    'fu-uefi-update-info.c',
    backend_srcs,
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    plugin_deps,
    platform_deps,
    efiboot,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  install : true,
  install_dir : bindir,
  c_args : cargs,
)
endif

if get_option('compat_cli') and get_option('man')
  configure_file(
    input : 'fwupdate.1',
    output : 'fwupdate.1',
    configuration : conf,
    install: true,
    install_dir: join_paths(mandir, 'man1'),
  )
endif

install_data(['uefi_capsule.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)

# add all the .po files as inputs to watch
ux_linguas = run_command(
  'cat', files(join_paths(meson.source_root(), 'po', 'LINGUAS')),
).stdout().strip().split('\n')
ux_capsule_pofiles = []
foreach ux_lingua : ux_linguas
  ux_capsule_pofiles += join_paths(meson.source_root(), 'po', '@0@.po'.format(ux_lingua))
endforeach

if get_option('plugin_uefi_capsule_splash')
  # add the archive of pregenerated images
  custom_target('ux-capsule-tar',
    input : [
      join_paths(meson.source_root(), 'po', 'LINGUAS'),
      join_paths(meson.current_source_dir(), 'make-images.py'),
      ux_capsule_pofiles,
    ],
    output : 'uefi-capsule-ux.tar.xz',
    command : [
      python3.path(),
      join_paths(meson.current_source_dir(), 'make-images.py'),
      '--podir', join_paths(meson.source_root(), 'po'),
      '--label', 'Installing firmware update…',
      '--out', '@OUTPUT@',
    ],
    install: true,
    install_dir: join_paths(datadir, 'fwupd'),
  )
endif

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'uefi-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-uefi-bgrt.c',
      'fu-uefi-bootmgr.c',
      'fu-uefi-common.c',
      'fu-uefi-device.c',
      'fu-uefi-cod-device.c',
      'fu-uefi-nvram-device.c',
      'fu-uefi-grub-device.c',
      'fu-uefi-devpath.c',
      'fu-uefi-update-info.c',
      'fu-ucs2.c',
      backend_srcs,
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      platform_deps,
      efiboot,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  test('uefi-self-test', e, env: env)

# to use these do `sudo systemctl edit fwupd.service` and set
# Environment="FWUPD_SYSFSFWDIR=/usr/share/installed-tests/fwupd"
install_data([
    'tests/efi/esrt/entries/entry0/capsule_flags',
    'tests/efi/esrt/entries/entry0/fw_class',
    'tests/efi/esrt/entries/entry0/fw_type',
    'tests/efi/esrt/entries/entry0/fw_version',
    'tests/efi/esrt/entries/entry0/last_attempt_status',
    'tests/efi/esrt/entries/entry0/last_attempt_version',
    'tests/efi/esrt/entries/entry0/lowest_supported_fw_version',
  ],
  install_dir : join_paths(installed_test_datadir, 'efi/esrt/entries/entry0'),
)
install_data([
    'tests/efi/efivars/CapsuleMax-39b68c46-f7fb-441b-b6ec-16b0f69821f3',
  ],
  install_dir : join_paths(installed_test_datadir, 'efi/efivars'),
)
endif

endif
