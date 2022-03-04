if get_option('plugin_uefi_capsule')
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDbx"']

install_data(['uefi-dbx.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_uefi_dbx',
  fu_hash,
  sources : [
    'fu-plugin-uefi-dbx.c',
    'fu-uefi-dbx-common.c',
    'fu-uefi-dbx-device.c',
    'fu-efi-image.c',
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
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'uefi-dbx-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-uefi-dbx-common.c',
      'fu-efi-image.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs,
    install : true,
    install_dir : installed_test_bindir,
  )
  test('uefi-dbx-self-test', e, env : env)  # added to installed-tests
endif

dbxtool = executable(
  'dbxtool',
  fu_hash,
  sources : [
    'fu-dbxtool.c',
    'fu-uefi-dbx-common.c',
    'fu-efi-image.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    plugin_deps,
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
  configure_file(
    input : 'dbxtool.1',
    output : 'dbxtool.1',
    configuration : conf,
    install: true,
    install_dir: join_paths(mandir, 'man1'),
  )
endif
endif
