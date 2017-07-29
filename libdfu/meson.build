cargs = [
  '-DG_LOG_DOMAIN="Dfu"',
]

install_headers('dfu.h')
install_headers([
    'dfu-common.h',
    'dfu-context.h',
    'dfu-device.h',
    'dfu-element.h',
    'dfu-error.h',
    'dfu-firmware.h',
    'dfu-image.h',
    'dfu-patch.h',
    'dfu-sector.h',
    'dfu-target.h',
  ],
  subdir : 'libdfu'
)

deps = [
  appstream_glib,
  giounix,
  libm,
  gusb,
]

if get_option('enable-libelf')
  deps += libelf
endif

mapfile = 'dfu.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)
dfu = shared_library(
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
  soversion : lt_current,
  version : lt_version,
  dependencies : deps,
  c_args : cargs,
  include_directories : include_directories('..'),
  link_args : vflag,
  link_depends : mapfile,
  install : true
)

pkgg = import('pkgconfig')
pkgg.generate(
  libraries : dfu,
  requires : [ 'gio-2.0', 'gusb' ],
  requires_private : [ 'libelf', 'appstream-glib' ],
  subdirs : 'libdfu',
  version : meson.project_version(),
  name : 'dfu',
  filebase : 'dfu',
  description : 'libdfu is a library for reading and writing USB device firmware',
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

if get_option('enable-introspection')
  gnome.generate_gir(dfu,
    sources : [
      'dfu-common.c',
      'dfu-common.h',
      'dfu-context.c',
      'dfu-context.h',
      'dfu-device.c',
      'dfu-device.h',
      'dfu-element.c',
      'dfu-element.h',
      'dfu-error.c',
      'dfu-error.h',
      'dfu-firmware.c',
      'dfu-firmware.h',
      'dfu-image.c',
      'dfu-image.h',
      'dfu-patch.c',
      'dfu-patch.h',
      'dfu-sector.c',
      'dfu-sector.h',
      'dfu-target.c',
      'dfu-target.h',
    ],
    nsversion : '1.0',
    namespace : 'Dfu',
    symbol_prefix : 'dfu',
    identifier_prefix : 'Dfu',
    export_packages : 'dfu',
    dependencies : deps,
    includes : [
      'AppStreamGlib-1.0',
      'Gio-2.0',
      'GObject-2.0',
      'GUsb-1.0',
    ],
    install : true
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
