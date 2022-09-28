if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']

plugin_quirks += files('dfu.quirk')

plugin_builtin_dfu = static_library('fu_plugin_dfu',
  sources: [
    'fu-dfu-plugin.c',
    'fu-dfu-common.c',
    'fu-dfu-device.c',
    'fu-dfu-sector.c',
    'fu-dfu-target.c',
    'fu-dfu-target-stm.c',
    'fu-dfu-target-avr.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libm,
  ],
  link_with: plugin_libs,
)
plugin_builtins += plugin_builtin_dfu

if get_option('compat_cli')
fu_dfu_tool = executable(
  'dfu-tool',
  sources: [
    'fu-dfu-tool.c',
  ],
  include_directories: plugin_incdirs,
  dependencies: plugin_deps,
  link_with: [
    plugin_builtin_dfu,
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  install: true,
  install_dir: bindir
)
endif

if get_option('compat_cli') and get_option('man')
  configure_file(
    input: 'dfu-tool.1',
    output: 'dfu-tool.1',
    configuration: conf,
    install: true,
    install_dir: join_paths(mandir, 'man1'),
  )
endif

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'fu-dfu-self-test',
    sources: [
      'fu-dfu-self-test.c'
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libm,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_dfu,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('fu-dfu-self-test', e, env: env)  # added to installed-tests
endif

plugindfu_incdir = include_directories('.')
endif
