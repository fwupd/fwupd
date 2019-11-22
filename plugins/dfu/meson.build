cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']

install_data(['dfu.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

dfu = static_library(
  'dfu',
  fu_hash,
  sources : [
    'dfu-common.c',
    'dfu-device.c',
    'dfu-element.c',
    'dfu-firmware.c',
    'dfu-format-dfu.c',
    'dfu-format-dfuse.c',
    'dfu-format-raw.c',
    'dfu-image.c',
    'dfu-sector.c',
    'dfu-target.c',
    'dfu-target-stm.c',
    'dfu-target-avr.c',
  ],
  dependencies : [
    giounix,
    libm,
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

dfu_tool = executable(
  'dfu-tool',
  fu_hash,
  sources : [
    'dfu-tool.c',
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
  extra = join_paths(meson.current_source_dir(), 'dfu-tool.h2m')
  custom_target('dfu-tool-man',
    input : dfu_tool,
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
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'dfu-self-test',
    fu_hash,
    sources : [
      'dfu-self-test.c'
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
    c_args : cargs
  )
  test('dfu-self-test', e)
endif

plugindfu_incdir = include_directories('.')
