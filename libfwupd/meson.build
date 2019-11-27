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
    'fwupd-deprecated.h',
    'fwupd-device.h',
    'fwupd-enums.h',
    'fwupd-error.h',
    'fwupd-remote.h',
    'fwupd-release.h',
    fwupd_version_h,
  ],
  subdir : 'fwupd-1/libfwupd',
)

fwupd_mapfile = 'fwupd.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), fwupd_mapfile)
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
  ],
  soversion : libfwupd_lt_current,
  version : libfwupd_lt_version,
  dependencies : [
    giounix,
    soup,
    libjsonglib,
  ],
  c_args : [
      cargs,
      '-DLOCALSTATEDIR="' + localstatedir + '"',
    ],
  include_directories : root_incdir,
  link_args : vflag,
  link_depends : fwupd_mapfile,
  install : true
)

pkgg = import('pkgconfig')
pkgg.generate(
  libraries : fwupd,
  requires : [ 'gio-2.0' ],
  subdirs : 'fwupd-1',
  version : meson.project_version(),
  name : 'fwupd',
  filebase : 'fwupd',
  description : 'fwupd is a system daemon for installing device firmware',
)

if get_option('introspection')
  fwupd_gir = gnome.generate_gir(fwupd,
    sources : [
      'fwupd-client.c',
      'fwupd-client.h',
      'fwupd-common.c',
      'fwupd-common.h',
      'fwupd-common-private.h',
      'fwupd-device.c',
      'fwupd-device.h',
      'fwupd-device-private.h',
      'fwupd-enums.c',
      'fwupd-enums.h',
      'fwupd-enums-private.h',
      'fwupd-error.c',
      'fwupd-error.h',
      'fwupd-release.c',
      'fwupd-release.h',
      'fwupd-release-private.h',
      'fwupd-remote.c',
      'fwupd-remote.h',
      'fwupd-remote-private.h',
    ],
    nsversion : '2.0',
    namespace : 'Fwupd',
    symbol_prefix : 'fwupd',
    identifier_prefix : 'Fwupd',
    export_packages : 'fwupd',
    header : 'fwupd.h',
    dependencies : [
      giounix,
      soup,
    ],
    includes : [
      'Gio-2.0',
      'GObject-2.0',
      'Soup-2.4',
    ],
    install : true
  )

  gnome.generate_vapi('fwupd',
    sources : fwupd_gir[0],
    packages : ['gio-2.0', 'libsoup-2.4'],
    install : true,
  )

  # Verify the map file is correct -- note we can't actually use the generated
  # file for two reasons:
  #
  #  1. We don't hard depend on GObject Introspection
  #  2. The map file is required to build the lib that the GIR is built from
  #
  # To avoid the circular dep, and to ensure we don't change exported API
  # accidentally actually check in a version of the version script to git.
  mapfile_target = custom_target('fwupd_mapfile',
    input: fwupd_gir[0],
    output: 'fwupd.map',
    command: [
      join_paths(meson.source_root(), 'contrib', 'generate-version-script.py'),
      'LIBFWUPD',
      '@INPUT@',
      '@OUTPUT@',
    ],
  )
  test('fwupd-exported-api', diffcmd,
       args : [
        '-urNp',
        join_paths(meson.current_source_dir(), 'fwupd.map'),
        mapfile_target,
        ],
      )
endif

if get_option('tests')
  testdatadir = join_paths(meson.source_root(), 'data')
  localremotetestdir = join_paths(meson.source_root(), 'plugins', 'dell-esrt')
  e = executable(
    'fwupd-self-test',
    sources : [
      'fwupd-self-test.c'
    ],
    include_directories : [
      root_incdir,
    ],
    dependencies : [
      gio,
      soup,
      libjsonglib,
    ],
    link_with : fwupd,
    c_args : [
      cargs,
      '-DLOCALSTATEDIR="' + localstatedir + '"',
      '-DTESTDATADIR="' + testdatadir + '"',
      '-DFU_SELF_TEST_REMOTES_DIR="' + testdatadir + '"',
      '-DFU_LOCAL_REMOTE_DIR="' + localremotetestdir + '"',
    ],
  )
  test('fwupd-self-test', e)
endif

fwupd_incdir = include_directories('.')
