cargs = ['-DG_LOG_DOMAIN="FuPluginSnapdUefi"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_snapd_uefi = static_library('fu_plugin_snapd_uefi',
  sources: [
    'fu-snapd-uefi-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libcurl,
  ],
)
plugin_builtins += plugin_builtin_snapd_uefi

if get_option('tests')
  install_data(['tests/snapd.py'],
    install_dir: join_paths(installed_test_datadir, 'tests'))

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('G_DEBUG', 'fatal-criticals')
  e = executable(
    'snap-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libcurl,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_snapd_uefi,
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
  test('snap-self-test', e, env: env) # added to installed-tests
endif
