if allow_uefi_capsule
subdir('tests')
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiCapsule"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

efi_os_dir = get_option('efi_os_dir')
if efi_os_dir != ''
  cargs += '-DEFI_OS_DIR="' + efi_os_dir + '"'
endif

plugin_quirks += files('uefi-capsule.quirk')

backend_srcs = ['fu-uefi-capsule-backend.c']
if host_machine.system() == 'linux'
  backend_srcs += 'fu-uefi-capsule-backend-linux.c'
  # replace @localstatedir@
  con2 = configuration_data()
  con2.set('localstatedir', localstatedir)
  configure_file(
    input: 'fwupd.grub.conf.in',
    output: '35_fwupd',
    configuration: con2,
    install: true,
    install_dir: join_paths(sysconfdir, 'grub.d')
  )
elif host_machine.system() == 'freebsd'
  backend_srcs += 'fu-uefi-capsule-backend-freebsd.c'
else
  error('no ESRT support for @0@'.format(host_machine.system()))
endif

plugin_builtin_uefi_capsule = static_library('fu_plugin_uefi_capsule',
  rustgen.process('fu-uefi.rs'),
  sources: [
    'fu-uefi-capsule-plugin.c',
    'fu-uefi-bgrt.c',
    'fu-uefi-bootmgr.c',
    'fu-uefi-common.c',
    'fu-uefi-cod-device.c',
    'fu-uefi-nvram-device.c',
    'fu-uefi-grub-device.c',
    'fu-uefi-capsule-device.c',
    'fu-uefi-update-info.c',
    'fu-acpi-uefi.c',
    'fu-bitmap-image.c',
    backend_srcs,
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    platform_deps,
  ],
)
plugin_builtins += plugin_builtin_uefi_capsule

# add all the .po files as inputs to watch
ux_linguas = run_command(
  ['cat', files(join_paths(meson.project_source_root(), 'po', 'LINGUAS'))],
  check: true,
).stdout().strip().split('\n')
ux_capsule_pofiles = []
foreach ux_lingua: ux_linguas
  ux_capsule_pofiles += join_paths(meson.project_source_root(), 'po', '@0@.po'.format(ux_lingua))
endforeach

if get_option('efi_binary')
  efi_binary = dependency('fwupd-efi', version: '>= 1.6', fallback: ['fwupd-efi', 'fwupd_efi_dep'])
endif

ux_capsule_tar = []
if get_option('plugin_uefi_capsule_splash')
  # add the archive of pregenerated images
  splash_deps = run_command([
    python3,
    join_paths(meson.project_source_root(), 'po', 'test-deps'),
    join_paths(meson.project_source_root(), 'po', 'LINGUAS'),
  ], check: false)
  if splash_deps.returncode() != 0
    error(splash_deps.stderr().strip())
  endif
  ux_capsule_tar = custom_target('ux-capsule-tar',
    input: [
      join_paths(meson.project_source_root(), 'po', 'LINGUAS'),
      files('make-images.py'),
      ux_capsule_pofiles,
    ],
    output: 'uefi-capsule-ux.tar.xz',
    command: [
      python3.full_path(),
      files('make-images.py'),
      '--podir', join_paths(meson.project_source_root(), 'po'),
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
    rustgen.process('fu-uefi.rs'),
    uefi_insyde_blob,
    fwupdx64_efi_signed,
    ux_capsule_tar,
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      platform_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_uefi_capsule,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
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
  install_dir: join_paths(installed_test_datadir, 'efi/esrt/entries/entry0'),
)
endif

summary({
  'efi_os_dir': efi_os_dir,
  'efi binary': get_option('efi_binary'),
  'capsule splash': get_option('plugin_uefi_capsule_splash'),
}, section:'uefi capsule options')
endif
