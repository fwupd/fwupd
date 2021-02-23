if get_option('gusb')
cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']

install_data(['dfu.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

dfu = static_library(
  'dfu',
  fu_hash,
  sources : [
    'fu-dfu-common.c',
    'fu-dfu-device.c',
    'fu-dfu-sector.c',
    'fu-dfu-target.c',
    'fu-dfu-target-stm.c',
    'fu-dfu-target-avr.c',
  ],
  dependencies : [
    giounix,
    libm,
    libxmlb,
    gusb,
    gudev,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
)

shared_module('fu_plugin_dfu',
  fu_hash,
  sources : [
    'fu-plugin-dfu.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
    dfu,
  ],
)

fu_dfu_tool = executable(
  'dfu-tool',
  fu_hash,
  sources : [
    'fu-dfu-tool.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    libxmlb,
    giounix,
    libm,
    gusb,
    gudev,
  ],
  link_with : [
    dfu,
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  install : true,
  install_dir : bindir
)

if get_option('man')
  help2man = find_program('help2man')
  extra = join_paths(meson.current_source_dir(), 'fu-dfu-tool.h2m')
  custom_target('dfu-tool-man',
    input : fu_dfu_tool,
    output : 'dfu-tool.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'dfu-tool',
      '--manual', 'User Commands',
      '--version-string', fwupd_version,
      '--include', extra,
    ],
    install : true,
    install_dir : join_paths(mandir, 'man1'),
  )
endif

if get_option('tests')
  testdatadirs = environment()
  testdatadirs.set('G_TEST_SRCDIR', meson.current_source_dir())
  testdatadirs.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'fu-dfu-self-test',
    fu_hash,
    sources : [
      'fu-dfu-self-test.c'
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      libxmlb,
      gio,
      gusb,
      gudev,
      libm,
    ],
    link_with : [
      dfu,
      fwupd,
      fwupdplugin,
    ],
    install : true,
    install_dir : installed_test_bindir,
  )
  test('fu-dfu-self-test', e, env : testdatadirs)  # added to installed-tests
endif

plugindfu_incdir = include_directories('.')
endif
