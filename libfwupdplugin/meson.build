if get_option('tests')
subdir('tests')
endif

fwupdplugin_version_h = configure_file(
  input : 'fu-version.h.in',
  output : 'fu-version.h',
  configuration : conf
)

fwupdplugin_src = [
  'fu-archive.c',
  'fu-backend.c',
  'fu-bluez-device.c',
  'fu-cabinet.c',
  'fu-chunk.c',             # fuzzing
  'fu-common.c',            # fuzzing
  'fu-common-cab.c',
  'fu-common-guid.c',
  'fu-common-version.c',    # fuzzing
  'fu-context.c',           # fuzzing
  'fu-device-locker.c',     # fuzzing
  'fu-device.c',            # fuzzing
  'fu-dfu-firmware.c',      # fuzzing
  'fu-cfu-common.c',        # fuzzing
  'fu-cfu-offer.c',         # fuzzing
  'fu-cfu-payload.c',       # fuzzing
  'fu-volume.c',            # fuzzing
  'fu-firmware.c',          # fuzzing
  'fu-firmware-common.c',   # fuzzing
  'fu-dfuse-firmware.c',    # fuzzing
  'fu-fmap-firmware.c',     # fuzzing
  'fu-hwids.c',             # fuzzing
  'fu-ihex-firmware.c',     # fuzzing
  'fu-io-channel.c',        # fuzzing
  'fu-plugin.c',
  'fu-quirks.c',            # fuzzing
  'fu-progress.c',          # fuzzing
  'fu-security-attrs.c',
  'fu-smbios.c',            # fuzzing
  'fu-srec-firmware.c',     # fuzzing
  'fu-archive-firmware.c',
  'fu-kenv.c',              # fuzzing
  'fu-efi-signature.c',
  'fu-efi-signature-list.c',
  'fu-efi-common.c',              # fuzzing
  'fu-efi-firmware-common.c',     # fuzzing
  'fu-efi-firmware-file.c',       # fuzzing
  'fu-efi-firmware-filesystem.c', # fuzzing
  'fu-efi-firmware-section.c',    # fuzzing
  'fu-efi-firmware-volume.c',     # fuzzing
  'fu-ifd-bios.c',                # fuzzing
  'fu-ifd-common.c',              # fuzzing
  'fu-ifd-firmware.c',            # fuzzing
  'fu-ifd-image.c',               # fuzzing
  'fu-uswid-firmware.c',          # fuzzing
  'fu-coswid-firmware.c',         # fuzzing
  'fu-efivar.c',
  'fu-udev-device.c',
  'fu-i2c-device.c',
  'fu-usb-device.c',
  'fu-cfi-device.c',
  'fu-hid-device.c',
  'fu-version.c',
]

if host_machine.system() == 'linux'
  fwupdplugin_src += 'fu-common-linux.c'    # fuzzing
  fwupdplugin_src += 'fu-efivar-linux.c'
elif host_machine.system() == 'freebsd'
  fwupdplugin_src += 'fu-common-freebsd.c'
  fwupdplugin_src += 'fu-efivar-freebsd.c'
elif host_machine.system() == 'windows'
  fwupdplugin_src += 'fu-common-windows.c'
  fwupdplugin_src += 'fu-efivar-windows.c'  # fuzzing
else
  error('no OS support for @0@'.format(host_machine.system()))
endif

fwupdplugin_headers = [
  'fu-archive.h',
  'fu-backend.h',
  'fu-bluez-device.h',
  'fu-cabinet.h',
  'fu-chunk.h',
  'fu-common.h',
  'fu-common-cab.h',
  'fu-common-guid.h',
  'fu-common-version.h',
  'fu-context.h',
  'fu-deprecated.h',
  'fu-device.h',
  'fu-device-metadata.h',
  'fu-device-locker.h',
  'fu-dfu-firmware.h',
  'fu-cfu-common.h',
  'fu-cfu-offer.h',
  'fu-cfu-payload.h',
  'fu-efi-common.h',
  'fu-efi-firmware-file.h',
  'fu-efi-firmware-filesystem.h',
  'fu-efi-firmware-section.h',
  'fu-efi-firmware-volume.h',
  'fu-ifd-bios.h',
  'fu-ifd-common.h',
  'fu-ifd-firmware.h',
  'fu-uswid-firmware.h',
  'fu-coswid-firmware.h',
  'fu-ifd-image.h',
  'fu-volume.h',
  'fu-firmware.h',
  'fu-firmware-common.h',
  'fu-fmap-firmware.h',
  'fu-dfuse-firmware.h',
  'fu-hwids.h',
  'fu-ihex-firmware.h',
  'fu-io-channel.h',
  'fu-plugin.h',
  'fu-quirks.h',
  'fu-security-attrs.h',
  'fu-progress.h',
  'fu-smbios.h',
  'fu-cfi-device.h',
  'fu-srec-firmware.h',
  'fu-archive-firmware.h',
  'fu-efi-signature.h',
  'fu-efi-signature-list.h',
  'fu-efivar.h',
  'fu-udev-device.h',
  'fu-i2c-device.h',
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
  command : [python3.full_path(),
             join_paths(meson.current_source_dir(), 'fu-hash.py'),
             '@OUTPUT@', '@INPUT@']
)

fwupdplugin_headers_private = [
  fu_hash,
  'fu-context-private.h',
  'fu-device-private.h',
  'fu-kenv.h',
  'fu-plugin-private.h',
  'fu-security-attrs-private.h',
  'fu-smbios-private.h',
  'fu-udev-device-private.h',
  'fu-usb-device-private.h',
  fwupdplugin_version_h,
]

introspection_deps = [
  libxmlb,
  libjcat,
  giounix,
  gusb,
  gudev,
]

pkgg_requires = [ 'gio-2.0',
                  'gmodule-2.0',
                  'gobject-2.0',
                  'fwupd',
                  'json-glib-1.0',
                  'libarchive',
                  'libgcab-1.0',
                  'xmlb',
                  'jcat',
                  'gusb'
]

library_deps = [
  introspection_deps,
  gmodule,
  libjsonglib,
  libgcab,
  valgrind,
  libjcat,
  lzma,
  libarchive,
  cbor,
  platform_deps,
]

fwupdplugin_mapfile = 'fwupdplugin.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), fwupdplugin_mapfile)
fwupdplugin = library(
  'fwupdplugin',
  sources : [
    fwupdplugin_src,
    fwupdplugin_headers,
    fwupdplugin_headers_private,
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
  requires : pkgg_requires,
  subdirs : 'fwupd-1',
  version : meson.project_version(),
  name : 'fwupdplugin',
  filebase : 'fwupdplugin',
  description : 'library for plugins to use to interact with fwupd daemon',
)

if introspection.allowed()
  gir_dep = declare_dependency(sources: fwupd_gir)
  girtargets = []
  extra_args = []
  if gusb.found()
    if gusb.type_name() == 'internal'
      girtargets += subproject('gusb').get_variable('libgusb_girtarget')[0]
    else
      girtargets += 'GUsb-1.0'
    endif
    extra_args += '-DHAVE_GUSB'
  endif
  if libxmlb.type_name() == 'internal'
    girtargets += subproject('libxmlb').get_variable('gir')[0]
  elif libxmlb.version().version_compare ('>= 0.3.2')
    girtargets += 'Xmlb-2.0'
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
    export_packages : 'fwupdplugin',
    extra_args : extra_args,
    include_directories : [
      fwupd_incdir,
    ],
    header : 'fwupdplugin.h',
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
      girtargets,
      fwupd_gir[0],
    ],
    install : true
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
      python3,
      join_paths(meson.project_source_root(), 'contrib', 'generate-version-script.py'),
      'LIBFWUPDPLUGIN',
      '@INPUT@',
      '@OUTPUT@',
      '--override', 'fu_chunk_get_type', '1.5.6',
      '--override', 'fu_srec_firmware_record_get_type', '1.6.1',
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
    fu_hash,
  ]
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
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
    ],
  )
  test('fwupdplugin-self-test', e, is_parallel:false, timeout:180, env : env)
endif

fwupdplugin_incdir = include_directories('.')
