host_machine.system() == 'linux' or subdir_done()
libmnl.found() or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginDevlink"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('devlink.quirk')
plugin_builtin_devlink = static_library('fu_plugin_devlink',
  sources: [
    'fu-devlink-plugin.c',
    'fu-devlink-backend.c',
    'fu-devlink-device.c',
    'fu-devlink-component.c',
    'fu-devlink-netlink.c',
  ],
  include_directories: plugin_incdirs,
  link_with: [
    fwupdplugin,
    fwupd,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
    valgrind,
    libmnl,
  ],
)
plugin_builtins += plugin_builtin_devlink

device_tests += files(
  'tests/devlink-netdevsim.json',
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'devlink-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      valgrind,
      libmnl,
    ],
    link_with: [
      plugin_builtin_devlink,
      fwupdplugin,
      fwupd,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
  )
  test('devlink-self-test', e, env: env)
endif
