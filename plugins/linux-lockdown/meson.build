host_machine.system() == 'linux' or subdir_done()
hsi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxLockdown"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_linux_lockdown = static_library('fu_plugin_linux_lockdown',
  rustgen.process('fu-linux-lockdown.rs'),
  sources: [
    'fu-linux-lockdown-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_linux_lockdown

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'linux-lockdown-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_linux_lockdown,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('linux-lockdown-self-test', e, env: env)
endif
