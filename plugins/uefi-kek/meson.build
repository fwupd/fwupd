allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiKek"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('uefi-kek.quirk')
plugin_builtin_uefi_kek = static_library('fu_plugin_uefi_kek',
  sources: [
    'fu-uefi-kek-device.c',
    'fu-uefi-kek-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_uefi_kek

if get_option('tests')
  e = executable(
    'uefi-kek-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_kek,
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
    'uefi-kek-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
