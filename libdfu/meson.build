cargs = [
  '-DG_LOG_DOMAIN="Dfu"',
]

deps = [
  appstream_glib,
  giounix,
  libm,
  gusb,
]

if get_option('enable-libelf')
  deps += libelf
endif

dfu = static_library(
  'dfu',
  sources : [
    'dfu-cipher-devo.c',
    'dfu-cipher-xtea.c',
    'dfu-common.c',
    'dfu-context.c',
    'dfu-device.c',
    'dfu-element.c',
    'dfu-error.c',
    'dfu-firmware.c',
    'dfu-format-dfu.c',
    'dfu-format-dfuse.c',
    'dfu-format-elf.c',
    'dfu-format-ihex.c',
    'dfu-format-metadata.c',
    'dfu-format-raw.c',
    'dfu-image.c',
    'dfu-patch.c',
    'dfu-sector.c',
    'dfu-target.c',
  ],
  dependencies : deps,
  c_args : cargs,
  include_directories : include_directories('..'),
)

executable(
  'dfu-tool',
  sources : [
    'dfu-progress-bar.c',
    'dfu-tool.c',
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : deps,
  link_with : dfu,
  c_args : cargs,
  install : true,
  install_dir : get_option('bindir')
)

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
    ],
    dependencies : [
      appstream_glib,
      gio,
      gusb,
      libm,
    ],
    link_with : dfu,
    c_args : cargs
  )
  test('dfu-self-test', e)
endif
