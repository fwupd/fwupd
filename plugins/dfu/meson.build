cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']

install_data(['dfu.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

dfu = static_library(
  'dfu',
  sources : [
    'dfu-chunked.c',
    'dfu-cipher-xtea.c',
    'dfu-common.c',
    'dfu-context.c',
    'dfu-device.c',
    'dfu-element.c',
    'dfu-firmware.c',
    'dfu-format-dfu.c',
    'dfu-format-dfuse.c',
    'dfu-format-ihex.c',
    'dfu-format-metadata.c',
    'dfu-format-raw.c',
    'dfu-image.c',
    'dfu-patch.c',
    'dfu-sector.c',
    'dfu-target.c',
    'dfu-target-stm.c',
    'dfu-target-avr.c',
  ],
  dependencies : [
    appstream_glib,
    giounix,
    libm,
    gusb,
  ],
  c_args : cargs,
  include_directories : [
    include_directories('../..'),
    include_directories('../../libfwupd'),
    include_directories('../../src'),
  ],
)

shared_module('fu_plugin_dfu',
  sources : [
    'fu-plugin-dfu.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    dfu,
  ],
)

executable(
  'dfu-tool',
  sources : [
    'dfu-tool.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  dependencies : [
    appstream_glib,
    giounix,
    libm,
    gusb,
  ],
  link_with : [
    dfu,
    fwupd,
    libfwupdprivate,
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('bindir')
)

if get_option('enable-man')
  docbook2man = find_program('docbook2man')
  custom_target('dfu-tool-man',
    input : 'dfu-tool.sgml',
    output : 'dfu-tool.1',
    command : [
      docbook2man, '@INPUT@',
      '--output', meson.current_build_dir(),
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1'),
  )
endif

if get_option('enable-tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'dfu-self-test',
    sources : [
      'dfu-self-test.c'
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
      include_directories('../../libfwupd'),
      include_directories('../../src'),
    ],
    dependencies : [
      appstream_glib,
      gio,
      gusb,
      libm,
    ],
    link_with : [
      dfu,
      fwupd,
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('dfu-self-test', e)
endif
