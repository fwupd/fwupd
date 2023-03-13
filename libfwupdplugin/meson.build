if get_option('tests')
subdir('tests')
endif

fwupdplugin_version_h = configure_file(
  input: 'fu-version.h.in',
  output: 'fu-version.h',
  configuration: conf
)

fwupdplugin_src = [
  'fu-acpi-table.c',        # fuzzing
  'fu-archive.c',
  'fu-backend.c',
  'fu-bluez-device.c',
  'fu-cabinet.c',
  'fu-chunk.c',             # fuzzing
  'fu-common.c',            # fuzzing
  'fu-sum.c',               # fuzzing
  'fu-struct.c',            # fuzzing
  'fu-crc.c',               # fuzzing
  'fu-mem.c',               # fuzzing
  'fu-byte-array.c',        # fuzzing
  'fu-string.c',            # fuzzing
  'fu-bytes.c',             # fuzzing
  'fu-kernel.c',            # fuzzing
  'fu-dump.c',              # fuzzing
  'fu-path.c',              # fuzzing
  'fu-common-guid.c',
  'fu-version-common.c',    # fuzzing
  'fu-context.c',           # fuzzing
  'fu-device-locker.c',     # fuzzing
  'fu-device.c',            # fuzzing
  'fu-device-progress.c',
  'fu-dfu-firmware.c',      # fuzzing
  'fu-fdt-firmware.c',      # fuzzing
  'fu-fdt-image.c',         # fuzzing
  'fu-fit-firmware.c',      # fuzzing
  'fu-ifwi-cpd-firmware.c', # fuzzing
  'fu-ifwi-fpt-firmware.c', # fuzzing
  'fu-oprom-firmware.c',    # fuzzing
  'fu-intel-thunderbolt-nvm.c',         # fuzzing
  'fu-intel-thunderbolt-firmware.c', # fuzzing
  'fu-cfu-common.c',        # fuzzing
  'fu-cfu-offer.c',         # fuzzing
  'fu-cfu-payload.c',       # fuzzing
  'fu-volume.c',            # fuzzing
  'fu-firmware.c',          # fuzzing
  'fu-bios-settings.c',        # fuzzing
  'fu-firmware-common.c',   # fuzzing
  'fu-dfuse-firmware.c',    # fuzzing
  'fu-fmap-firmware.c',     # fuzzing
  'fu-hwids.c',             # fuzzing
  'fu-hwids-config.c',      # fuzzing
  'fu-hwids-dmi.c',         # fuzzing
  'fu-hwids-fdt.c',         # fuzzing
  'fu-hwids-kenv.c',        # fuzzing
  'fu-hwids-smbios.c',      # fuzzing
  'fu-ihex-firmware.c',     # fuzzing
  'fu-io-channel.c',        # fuzzing
  'fu-plugin.c',
  'fu-quirks.c',            # fuzzing
  'fu-progress.c',          # fuzzing
  'fu-security-attr.c',     # fuzzing
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
  'fu-coswid-common.c',           # fuzzing
  'fu-coswid-firmware.c',         # fuzzing
  'fu-pefile-firmware.c',         # fuzzing
  'fu-efivar.c',
  'fu-udev-device.c',
  'fu-i2c-device.c',
  'fu-mei-device.c',
  'fu-usb-device.c',
  'fu-usb-device-ds20.c',         # fuzzing
  'fu-usb-device-fw-ds20.c',      # fuzzing
  'fu-usb-device-ms-ds20.c',      # fuzzing
  'fu-cfi-device.c',
  'fu-hid-device.c',
  'fu-linear-firmware.c',
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
elif host_machine.system() == 'darwin'
  fwupdplugin_src += 'fu-common-darwin.c'
  fwupdplugin_src += 'fu-efivar-darwin.c'
else
  error('no OS support for @0@'.format(host_machine.system()))
endif

fwupdplugin_headers = [
  'fu-acpi-table.h',
  'fu-archive.h',
  'fu-backend.h',
  'fu-bluez-device.h',
  'fu-cabinet.h',
  'fu-chunk.h',
  'fu-common.h',
  'fu-byte-array.h',
  'fu-string.h',
  'fu-sum.h',
  'fu-struct.h',
  'fu-crc.h',
  'fu-mem.h',
  'fu-dump.h',
  'fu-path.h',
  'fu-bytes.h',
  'fu-kernel.h',
  'fu-common-guid.h',
  'fu-version-common.h',
  'fu-context.h',
  'fu-device.h',
  'fu-device-metadata.h',
  'fu-device-locker.h',
  'fu-dfu-firmware.h',
  'fu-fdt-firmware.h',
  'fu-fdt-image.h',
  'fu-fit-firmware.h',
  'fu-ifwi-cpd-firmware.h',
  'fu-ifwi-fpt-firmware.h',
  'fu-oprom-firmware.h',
  'fu-intel-thunderbolt-firmware.h',
  'fu-intel-thunderbolt-nvm.h',
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
  'fu-pefile-firmware.h',
  'fu-ifd-image.h',
  'fu-linear-firmware.h',
  'fu-volume.h',
  'fu-firmware.h',
  'fu-bios-settings.h',
  'fu-firmware-common.h',
  'fu-fmap-firmware.h',
  'fu-dfuse-firmware.h',
  'fu-hwids.h',
  'fu-ihex-firmware.h',
  'fu-io-channel.h',
  'fu-plugin.h',
  'fu-quirks.h',
  'fu-security-attr.h',
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
  'fu-mei-device.h',
  'fu-usb-device.h',
  'fu-hid-device.h',
]

fwupdplugin_headers_private = [
  'fu-backend-private.h',
  'fu-context-private.h',
  'fu-device-private.h',
  'fu-device-progress.h',
  'fu-kenv.h',
  'fu-plugin-private.h',
  'fu-bios-settings-private.h',
  'fu-security-attrs-private.h',
  'fu-smbios-private.h',
  'fu-udev-device-private.h',
  'fu-usb-device-ds20.h',
  'fu-usb-device-fw-ds20.h',
  'fu-usb-device-ms-ds20.h',
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
  sources: [
    fwupdplugin_src,
    fwupdplugin_headers,
    fwupdplugin_headers_private,
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
  ],
  dependencies: [
    library_deps
  ],
  link_with: [
    fwupd,
  ],
  link_args: cc.get_supported_link_arguments([vflag]),
  link_depends: fwupdplugin_mapfile,
  install_dir: libdir_pkg,
  install: true
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
    sources: [
      fwupdplugin_src,
      fwupdplugin_headers,
      fwupdplugin_headers_private,
    ],
    nsversion: '1.0',
    namespace: 'FwupdPlugin',
    symbol_prefix: 'fu',
    identifier_prefix: 'Fu',
    export_packages: 'fwupdplugin',
    extra_args: extra_args,
    include_directories: [
      fwupd_incdir,
    ],
    header: 'fwupdplugin.h',
    dependencies: [
      gir_dep,
      introspection_deps
    ],
    link_with: [
      fwupdplugin,
    ],
    includes: [
      'Gio-2.0',
      'GObject-2.0',
      girtargets,
      fwupd_gir[0],
    ],
    install: false
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
       args: [
        '-urNp',
        files('fwupdplugin.map'),
        fwupdplugin_mapfile_target,
        ],
      )
endif

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'fwupdplugin-self-test',
    installed_firmware_zip,
    sources: [
      'fu-self-test.c'
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
    ],
    dependencies: [
      library_deps
    ],
    link_with: [
      fwupd,
      fwupdplugin
    ],
    c_args: [
    ],
  )
  test('fwupdplugin-self-test', e, is_parallel: false, timeout: 180, env: env)

  install_data([
      'tests/chassis_type',
      'tests/sys_vendor',
    ],
    install_dir: installed_test_datadir,
  )
  install_data([
      'tests/dmi/tables/DMI',
      'tests/dmi/tables/smbios_entry_point',
    ],
    install_dir: join_paths(installed_test_datadir, 'tests/dmi/tables'),
  )
  install_data([
      'tests/dmi/tables64/DMI',
      'tests/dmi/tables64/smbios_entry_point',
    ],
    install_dir: join_paths(installed_test_datadir, 'tests/dmi/tables64'),
  )
endif

fwupdplugin_incdir = include_directories('.')
