cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDbx"']

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
  testdatadirs = environment()
  testdatadirs.set('G_TEST_SRCDIR', meson.current_source_dir())
  testdatadirs.set('G_TEST_BUILDDIR', meson.current_build_dir())
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
  test('uefi-dbx-self-test', e, env : testdatadirs)  # added to installed-tests
endif

dbxtool = executable(
  'dbxtool',
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
  help2man = find_program('help2man')
  extra = join_paths(meson.current_source_dir(), 'dbxtool.h2m')
  custom_target('dbxtool-man',
    input : dbxtool,
    output : 'dbxtool.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'dbxtool',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
      '--include', extra,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
endif
