fwupdplugin_src = [
  'fu-archive.c',
  'fu-cabinet.c',
  'fu-chunk.c',
  'fu-common.c',
  'fu-common-cab.c',
  'fu-common-guid.c',
  'fu-common-version.c',
  'fu-device-locker.c',
  'fu-device.c',
  'fu-dfu-firmware.c',
  'fu-volume.c',
  'fu-firmware.c',
  'fu-firmware-common.c',
  'fu-firmware-image.c',
  'fu-fmap-firmware.c',
  'fu-hwids.c',
  'fu-ihex-firmware.c',
  'fu-io-channel.c',
  'fu-plugin.c',
  'fu-quirks.c',
  'fu-security-attrs.c',
  'fu-smbios.c',
  'fu-srec-firmware.c',
  'fu-efivar.c',
  'fu-udev-device.c',
  'fu-usb-device.c',
  'fu-hid-device.c',
]

fwupdplugin_headers = [
  'fu-archive.h',
  'fu-cabinet.h',
  'fu-chunk.h',
  'fu-common.h',
  'fu-common-cab.h',
  'fu-common-guid.h',
  'fu-common-version.h',
  'fu-device.h',
  'fu-device-metadata.h',
  'fu-device-locker.h',
  'fu-dfu-firmware.h',
  'fu-volume.h',
  'fu-firmware.h',
  'fu-firmware-common.h',
  'fu-firmware-image.h',
  'fu-fmap-firmware.h',
  'fu-hwids.h',
  'fu-ihex-firmware.h',
  'fu-io-channel.h',
  'fu-plugin.h',
  'fu-quirks.h',
  'fu-security-attrs.h',
  'fu-smbios.h',
  'fu-srec-firmware.h',
  'fu-efivar.h',
  'fu-udev-device.h',
  'fu-usb-device.h',
  'fu-hid-device.h',
]
install_headers(
  'fwupdplugin.h',
  subdir : 'fwupd-1',
)
install_headers([fwupdplugin_headers, 'fu-plugin-vfuncs.h'],
  subdir : 'fwupd-1/libfwupdplugin',
)

fu_hash = custom_target(
  'fu-hash.h',
  input : fwupdplugin_src,
  output : 'fu-hash.h',
  command : [python3.path(),
             join_paths(meson.current_source_dir(), 'fu-hash.py'),
             '@OUTPUT@', '@INPUT@']
)

fwupdplugin_headers_private = [
  fu_hash,
  'fu-device-private.h',
  'fu-plugin-private.h',
  'fu-security-attrs-private.h',
  'fu-smbios-private.h',
  'fu-usb-device-private.h',
]

introspection_deps = [
  libxmlb,
  libjcat,
  giounix,
  gusb,
]

if get_option('gudev')
  fwupdplugin_headers_private += 'fu-udev-device-private.h'
  introspection_deps += gudev
endif

library_deps = [
  introspection_deps,
  gmodule,
  libarchive,
  libjsonglib,
  libgcab,
  valgrind,
  libjcat,
  platform_deps,
]

fwupdplugin_mapfile = 'fwupdplugin.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), fwupdplugin_mapfile)
fwupdplugin = shared_library(
  'fwupdplugin',
  sources : [
    fwupdplugin_src,
    fwupdplugin_headers
  ],
  soversion : libfwupdplugin_lt_current,
  version : libfwupdplugin_lt_version,
  include_directories : [
    root_incdir,
    fwupd_incdir,
  ],
  dependencies : [
    library_deps
  ],
  link_with : [
    fwupd,
  ],
  link_args : vflag,
  link_depends : fwupdplugin_mapfile,
  install : true
)

fwupdplugin_pkgg = import('pkgconfig')
fwupdplugin_pkgg.generate(
  libraries : fwupdplugin,
  requires : [ 'gio-2.0',
               'gmodule-2.0',
               'gobject-2.0',
               'gusb',
               'fwupd',
               'json-glib-1.0',
               'libarchive',
               'libgcab-1.0',
               'xmlb',
               'jcat',
  ],
  subdirs : 'fwupd-1',
  version : meson.project_version(),
  name : 'fwupdplugin',
  filebase : 'fwupdplugin',
  description : 'library for plugins to use to interact with fwupd daemon',
)

if get_option('introspection')
  gir_dep = declare_dependency(sources: fwupd_gir)
  if gusb.type_name() == 'internal'
    libgusb_girtarget = subproject('gusb').get_variable('libgusb_girtarget')[0]
  else
    libgusb_girtarget = 'GUsb-1.0'
  endif
  fwupdplugin_gir = gnome.generate_gir(fwupd,
    sources : [
      fwupdplugin_src,
      fwupdplugin_headers,
      fwupdplugin_headers_private,
    ],
    nsversion : '1.0',
    namespace : 'FwupdPlugin',
    symbol_prefix : 'fu',
    identifier_prefix : 'Fu',
    export_packages : 'fu',
    include_directories : [
      fwupd_incdir,
    ],
    dependencies : [
      gir_dep,
      introspection_deps
    ],
    link_with : [
      fwupdplugin,
    ],
    includes : [
      'Gio-2.0',
      'GObject-2.0',
      libgusb_girtarget,
      fwupd_gir[0],
    ],
    install : true
  )
  gnome.generate_vapi('fwupdplugin',
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
  fwupdplugin_mapfile_target = custom_target('fwupdplugin_mapfile',
    input: fwupdplugin_gir[0],
    output: 'fwupdplugin.map',
    command: [
      join_paths(meson.source_root(), 'contrib', 'generate-version-script.py'),
      'LIBFWUPDPLUGIN',
      '@INPUT@',
      '@OUTPUT@',
    ],
  )

  test('fwupdplugin-exported-api', diffcmd,
       args : [
        '-urNp',
        join_paths(meson.current_source_dir(), 'fwupdplugin.map'),
        fwupdplugin_mapfile_target,
        ],
      )
endif

if get_option('tests')
  test_deps = [
    colorhug_test_firmware,
    builder_test_firmware,
    hwid_test_firmware,
    multiple_rels_test_firmware,
    noreqs_test_firmware,
    fu_hash,
  ]
  testdatadir_src = join_paths(meson.source_root(), 'data', 'tests')
  testdatadir_dst = join_paths(meson.build_root(), 'data', 'tests')
  pluginbuilddir = join_paths(meson.build_root(), 'plugins', 'test')
  e = executable(
    'fwupdplugin-self-test',
    test_deps,
    sources : [
      fwupdplugin_src,
      'fu-self-test.c'
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
    ],
    dependencies : [
      library_deps
    ],
    link_with : [
      fwupd,
      fwupdplugin
    ],
    c_args : [
      '-DTESTDATADIR_SRC="' + testdatadir_src + '"',
      '-DTESTDATADIR_DST="' + testdatadir_dst + '"',
      '-DPLUGINBUILDDIR="' + pluginbuilddir + '"',
    ],
  )
  test('fwupdplugin-self-test', e, is_parallel:false, timeout:180)
endif

fwupdplugin_incdir = include_directories('.')
