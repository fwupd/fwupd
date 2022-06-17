if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']

plugin_quirks += join_paths(meson.current_source_dir(), 'dfu.quirk')

dfu = static_library(
  'dfu',
  fu_hash,
  sources: [
    'fu-dfu-common.c',
    'fu-dfu-device.c',
    'fu-dfu-sector.c',
    'fu-dfu-target.c',
    'fu-dfu-target-stm.c',
    'fu-dfu-target-avr.c',
  ],
  dependencies: [
    plugin_deps,
    libm,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
)

shared_module('fu_plugin_dfu',
  fu_hash,
  sources: [
    'fu-plugin-dfu.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libm,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
    dfu,
  ],
)

if get_option('compat_cli')
fu_dfu_tool = executable(
  'dfu-tool',
  fu_hash,
  sources: [
    'fu-dfu-tool.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies: [
    plugin_deps,
  ],
  link_with: [
    dfu,
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
    fu_hash,
    sources: [
      'fu-dfu-self-test.c'
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
      libm,
    ],
    link_with: [
      dfu,
      fwupd,
      fwupdplugin,
    ],
    install: true,
    install_dir: installed_test_bindir,
  )
  test('fu-dfu-self-test', e, env: env)  # added to installed-tests
endif

plugindfu_incdir = include_directories('.')
endif
