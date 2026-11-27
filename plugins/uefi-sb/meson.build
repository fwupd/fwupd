allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiSb"']

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_quirks += files('uefi-sb-kek.quirk')
plugin_builtin_uefi_sb = static_library(
  'fu_plugin_uefi_sb',
  sources: [
    'fu-uefi-sb-db-device.c',
    'fu-uefi-sb-dbx-common.c',
    'fu-uefi-sb-dbx-device.c',
    'fu-uefi-sb-kek-device.c',
    'fu-uefi-sb-pk-device.c',
    'fu-uefi-sb-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_uefi_sb

if get_option('tests')
  e = executable(
    'uefi-sb-kek-self-test',
    sources: [
      'fu-uefi-sb-kek-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_sb,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
    ],
  )
  test(
    'uefi-sb-kek-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif

if get_option('tests')
  e = executable(
    'uefi-sb-dbx-self-test',
    sources: [
      'fu-uefi-sb-dbx-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libcurl,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_sb,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test(
    'uefi-sb-dbx-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
  install_data(
    [
      'uefi-sb-dbx.quirk',
    ],
    install_dir: installed_test_datadir,
  )
endif

dbxtool = executable(
  'dbxtool',
  sources: [
    'fu-dbxtool.c',
  ],
  include_directories: plugin_incdirs,
  dependencies: plugin_deps,
  link_with: [
    plugin_libs,
    plugin_builtin_uefi_sb,
  ],
  install: true,
  install_dir: bindir,
  install_rpath: libdir_pkg,
  c_args: cargs,
)

if get_option('man')
  custom_target(
    'dbxtool.1',
    input: 'dbxtool.md',
    output: 'dbxtool.1',
    command: [
      generate_man,
      '@INPUT@',
      '-o',
      '@OUTPUT@',
      '--replace',
      'PACKAGE_VERSION',
      fwupd_version,
    ],
    install: true,
    install_tag: 'man',
    install_dir: join_paths(mandir, 'man1'),
  )
endif
if build_docs
  md_targets += custom_target(
    'dbxtool.md',
    input: 'dbxtool.md',
    output: 'dbxtool.md',
    command: [
      generate_man,
      '@INPUT@',
      '-o',
      '@OUTPUT@',
      '--replace',
      'PACKAGE_VERSION',
      fwupd_version,
      '--md',
    ],
  )
  man_md += ['"dbxtool.md"']
endif

if get_option('tests')
  install_data(
    ['tests/quirks.d/uefi-db.quirk'],
    install_dir: join_paths(installed_test_datadir, 'tests', 'quirks.d'),
  )
  e = executable(
    'uefi-db-self-test',
    sources: [
      'fu-uefi-sb-db-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_sb,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test(
    'uefi-db-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
