if not allow_uefi
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiMok"']

plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_uefi_mok = static_library('fu_plugin_uefi_mok',
  rustgen.process('fu-uefi-mok.rs'),
  sources: [
    'fu-uefi-mok-common.c',
    'fu-uefi-mok-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_uefi_mok

if get_option('tests')
  install_data(
    [
      'tests/HSIStatus-nx-disabled',
      'tests/HSIStatus-nx-invalid',
      'tests/HSIStatus-nx-valid',
    ],
    install_dir: join_paths(installed_test_datadir, 'tests')
  )
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'uefi-mok-self-test',
    rustgen.process('fu-uefi-mok.rs'),
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
      plugin_builtin_uefi_mok,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('uefi-mok-self-test', e, env: env)
endif
