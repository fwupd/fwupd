cargs = [
  '-DG_LOG_DOMAIN="Fwupd"',
]

fwupd_version_h = configure_file(
  input : 'fwupd-version.h.in',
  output : 'fwupd-version.h',
  configuration : conf
)

install_headers(
  'fwupd.h',
  subdir : 'fwupd-1',
)

install_headers([
    'fwupd-client.h',
    'fwupd-common.h',
    'fwupd-device.h',
    'fwupd-enums.h',
    'fwupd-error.h',
    'fwupd-remote.h',
    'fwupd-release.h',
    'fwupd-result.h',
    fwupd_version_h,
  ],
  subdir : 'fwupd-1/libfwupd',
)

mapfile = 'fwupd.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)
fwupd = shared_library(
  'fwupd',
  sources : [
    'fwupd-client.c',
    'fwupd-common.c',
    'fwupd-device.c',
    'fwupd-enums.c',
    'fwupd-error.c',
    'fwupd-release.c',
    'fwupd-remote.c',
    'fwupd-result.c',
  ],
  soversion : lt_current,
  version : lt_version,
  dependencies : [
    giounix,
    soup,
  ],
  c_args : cargs,
  include_directories : include_directories('..'),
  link_args : vflag,
  link_depends : mapfile,
  install : true
)

pkgg = import('pkgconfig')
pkgg.generate(
  libraries : fwupd,
  requires : [ 'gio-2.0', 'libsoup-2.4' ],
  subdirs : 'fwupd-1',
  version : meson.project_version(),
  name : 'fwupd',
  filebase : 'fwupd',
  description : 'fwupd is a system daemon for installing device firmware',
)

gnome.generate_gir(fwupd,
  sources : [
    'fwupd-client.c',
    'fwupd-enums.c',
    'fwupd-enums.h',
    'fwupd-error.c',
    'fwupd-remote.c',
    'fwupd-result.c',
  ],
  nsversion : '1.0',
  namespace : 'Fwupd',
  symbol_prefix : 'fwupd',
  identifier_prefix : 'Fwupd',
  export_packages : 'fwupd',
  dependencies : [
    giounix,
    soup,
  ],
  includes : [
    'GObject-2.0',
  ],
  install : true
)

if get_option('enable-tests')
  testdatadir = join_paths(meson.source_root(), 'data')
  e = executable(
    'fwupd-self-test',
    sources : [
      'fwupd-self-test.c'
    ],
    include_directories : [
      include_directories('..'),
    ],
    dependencies : [
      gio,
      soup,
    ],
    link_with : fwupd,
    c_args : [
      cargs,
      '-DFU_SELF_TEST_REMOTES_DIR="' + testdatadir + '"',
    ],
  )
  test('fwupd-self-test', e)
endif
