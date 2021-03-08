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
    'fwupd-client-sync.h',
    'fwupd-common.h',
    'fwupd-deprecated.h',
    'fwupd-device.h',
    'fwupd-enums.h',
    'fwupd-error.h',
    'fwupd-remote.h',
    'fwupd-security-attr.h',
    'fwupd-release.h',
    'fwupd-plugin.h',
    fwupd_version_h,
  ],
  subdir : 'fwupd-1/libfwupd',
)

libfwupd_deps = [
    giounix,
    gmodule,
    libjcat,
    libjsonglib,
]

if get_option('curl')
  libfwupd_deps += libcurl
endif

libfwupd_src = [
  'fwupd-client.c',
  'fwupd-client-sync.c',
  'fwupd-common.c',         # fuzzing
  'fwupd-device.c',         # fuzzing
  'fwupd-enums.c',          # fuzzing
  'fwupd-error.c',          # fuzzing
  'fwupd-security-attr.c',
  'fwupd-release.c',        # fuzzing
  'fwupd-plugin.c',
  'fwupd-remote.c',
]

fwupd_mapfile = 'fwupd.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), fwupd_mapfile)
fwupd = library(
  'fwupd',
  sources : libfwupd_src,
  soversion : libfwupd_lt_current,
  version : libfwupd_lt_version,
  dependencies : libfwupd_deps,
  c_args : [
      '-DG_LOG_DOMAIN="Fwupd"',
      '-DLOCALSTATEDIR="' + localstatedir + '"',
    ],
  include_directories : root_incdir,
  link_args : vflag,
  link_depends : fwupd_mapfile,
  install : true
)

libfwupd_dep = declare_dependency(
  link_with : fwupd,
  include_directories : [root_incdir, include_directories('.')],
  dependencies : libfwupd_deps
)

pkgg = import('pkgconfig')
pkgg.generate(
  fwupd,
  requires : [ 'gio-2.0' ],
  subdirs : 'fwupd-1',
  version : meson.project_version(),
  name : 'fwupd',
  filebase : 'fwupd',
  description : 'fwupd is a system daemon for installing device firmware',
)

if get_option('introspection')
  fwupd_gir_deps = [
      giounix,
  ]
  if get_option('curl')
    fwupd_gir_deps += libcurl
  endif
  fwupd_gir = gnome.generate_gir(fwupd,
    sources : [
      'fwupd-client.c',
      'fwupd-client.h',
      'fwupd-client-sync.c',
      'fwupd-client-sync.h',
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
      'fwupd-security-attr.c',
      'fwupd-security-attr.h',
      'fwupd-security-attr-private.h',
      'fwupd-release.c',
      'fwupd-release.h',
      'fwupd-release-private.h',
      'fwupd-plugin.c',
      'fwupd-plugin.h',
      'fwupd-plugin-private.h',
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
    dependencies : fwupd_gir_deps,
    includes : [
      'Gio-2.0',
      'GObject-2.0',
    ],
    install : true
  )

  gnome.generate_vapi('fwupd',
    sources : fwupd_gir[0],
    packages : ['gio-2.0'],
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
      python3,
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
      libfwupd_deps,
    ],
    link_with : fwupd,
    c_args : [
      '-DG_LOG_DOMAIN="Fwupd"',
      '-DLOCALSTATEDIR="' + localstatedir + '"',
      '-DTESTDATADIR="' + testdatadir + '"',
      '-DFU_SELF_TEST_REMOTES_DIR="' + testdatadir + '"',
      '-DFU_LOCAL_REMOTE_DIR="' + localremotetestdir + '"',
    ],
  )
  test('fwupd-self-test', e, timeout: 60)
  if gio.version().version_compare ('>= 2.64.0')
    e = executable(
      'fwupd-thread-test',
      sources : [
        'fwupd-thread-test.c'
      ],
      include_directories : [
        root_incdir,
      ],
      dependencies : [
        libfwupd_deps,
      ],
      link_with : fwupd,
      c_args : [
        '-DG_LOG_DOMAIN="Fwupd"',
      ],
    )
    test('fwupd-thread-test', e, timeout: 60)
    e = executable(
      'fwupd-context-test',
      sources : [
        'fwupd-context-test.c'
      ],
      include_directories : [
        root_incdir,
      ],
      dependencies : [
        libfwupd_deps,
      ],
      link_with : fwupd,
      c_args : [
        '-DG_LOG_DOMAIN="Fwupd"',
      ],
    )
    test('fwupd-context-test', e, timeout: 60)
  endif
endif

fwupd_incdir = include_directories('.')
