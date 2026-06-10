hsi or subdir_done()
has_cpuid or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginMsr"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

if libsystemd.found()
  install_data(['fwupd-msr.conf'],
    install_tag: 'runtime',
    install_dir: systemd_modules_load_dir,
  )
endif

plugin_builtin_msr = static_library('fu_plugin_msr',
  sources: [
    'fu-msr-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_msr

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'msr-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_msr,
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
  test('msr-self-test', e, env: env)
endif
