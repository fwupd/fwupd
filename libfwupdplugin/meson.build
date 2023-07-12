if get_option('tests')
subdir('tests')
endif

fwupdplugin_version_h = configure_file(
  input: 'fu-version.h.in',
  output: 'fu-version.h',
  configuration: conf
)

fwupdplugin_structs = [
  'fu-acpi-table.rs', # fuzzing
  'fu-cfu-firmware.rs', # fuzzing
  'fu-coswid.rs', # fuzzing
  'fu-dfu-firmware.rs', # fuzzing
  'fu-efi.rs', # fuzzing
  'fu-elf.rs', # fuzzing
  'fu-fdt.rs', # fuzzing
  'fu-fmap.rs', # fuzzing
  'fu-ifd.rs', # fuzzing
  'fu-ifwi.rs', # fuzzing
  'fu-intel-thunderbolt.rs', # fuzzing
  'fu-oprom.rs', # fuzzing
  'fu-pefile.rs', # fuzzing
  'fu-sbatlevel-section.rs', # fuzzing
  'fu-smbios.rs', # fuzzing
  'fu-usb-device-ds20.rs', # fuzzing
  'fu-uswid.rs', # fuzzing
]

# do not use structgen as these are referenced in headers too...
fwupdplugin_struct_targets = []
foreach fwupdplugin_struct: fwupdplugin_structs
  fwupdplugin_struct_targets += custom_target('lib' + fwupdplugin_struct,
    input: fwupdplugin_struct,
    output: [
      fwupdplugin_struct.replace('.rs', '-struct.c'),
      fwupdplugin_struct.replace('.rs', '-struct.h'),
    ],
    command: [
      python3,
      join_paths(meson.project_source_root(), 'libfwupdplugin', 'rustgen.py'),
      '@INPUT@', '@OUTPUT0@', '@OUTPUT1@',
    ],
  )
endforeach

fwupdplugin_src = [
  'fu-acpi-table.c', # fuzzing
  'fu-archive.c',
  'fu-archive-firmware.c',
  'fu-backend.c',
  'fu-bios-settings.c', # fuzzing
  'fu-bluez-device.c',
  'fu-byte-array.c', # fuzzing
  'fu-bytes.c', # fuzzing
  'fu-cabinet.c',
  'fu-cfi-device.c',
  'fu-cfu-common.c', # fuzzing
  'fu-cfu-offer.c', # fuzzing
  'fu-cfu-payload.c', # fuzzing
  'fu-chunk.c', # fuzzing
  'fu-common.c', # fuzzing
  'fu-common-guid.c',
  'fu-config.c', # fuzzing
  'fu-context.c', # fuzzing
  'fu-coswid-firmware.c', # fuzzing
  'fu-crc.c', # fuzzing
  'fu-csv-entry.c', # fuzzing
  'fu-csv-firmware.c', # fuzzing
  'fu-device.c', # fuzzing
  'fu-device-locker.c', # fuzzing
  'fu-device-progress.c',
  'fu-dfu-firmware.c', # fuzzing
  'fu-dfuse-firmware.c', # fuzzing
  'fu-dump.c', # fuzzing
  'fu-efi-common.c', # fuzzing
  'fu-efi-device-path.c', # fuzzing
  'fu-efi-device-path-list.c', # fuzzing
  'fu-efi-file-path-device-path.c', # fuzzing
  'fu-efi-firmware-common.c', # fuzzing
  'fu-efi-firmware-file.c', # fuzzing
  'fu-efi-firmware-filesystem.c', # fuzzing
  'fu-efi-firmware-section.c', # fuzzing
  'fu-efi-firmware-volume.c', # fuzzing
  'fu-efi-hard-drive-device-path.c', # fuzzing
  'fu-efi-load-option.c', # fuzzing
  'fu-efi-signature.c',
  'fu-efi-signature-list.c',
  'fu-efivar.c', # fuzzing
  'fu-elf-firmware.c', # fuzzing
  'fu-fdt-firmware.c', # fuzzing
  'fu-fdt-image.c', # fuzzing
  'fu-firmware.c', # fuzzing
  'fu-firmware-common.c', # fuzzing
  'fu-fit-firmware.c', # fuzzing
  'fu-fmap-firmware.c', # fuzzing
  'fu-hid-device.c',
  'fu-hwids.c', # fuzzing
  'fu-hwids-config.c', # fuzzing
  'fu-hwids-dmi.c', # fuzzing
  'fu-hwids-fdt.c', # fuzzing
  'fu-hwids-kenv.c', # fuzzing
  'fu-hwids-smbios.c', # fuzzing
  'fu-i2c-device.c',
  'fu-ifd-bios.c', # fuzzing
  'fu-ifd-common.c', # fuzzing
  'fu-ifd-firmware.c', # fuzzing
  'fu-ifd-image.c', # fuzzing
  'fu-ifwi-cpd-firmware.c', # fuzzing
  'fu-ifwi-fpt-firmware.c', # fuzzing
  'fu-ihex-firmware.c', # fuzzing
  'fu-intel-thunderbolt-firmware.c', # fuzzing
  'fu-intel-thunderbolt-nvm.c', # fuzzing
  'fu-io-channel.c', # fuzzing
  'fu-kenv.c', # fuzzing
  'fu-kernel.c', # fuzzing
  'fu-linear-firmware.c',
  'fu-mei-device.c',
  'fu-mem.c', # fuzzing
  'fu-oprom-firmware.c', # fuzzing
  'fu-path.c', # fuzzing
  'fu-pefile-firmware.c', # fuzzing
  'fu-plugin.c',
  'fu-progress.c', # fuzzing
  'fu-quirks.c', # fuzzing
  'fu-sbatlevel-section.c', # fuzzing
  'fu-security-attr.c', # fuzzing
  'fu-security-attrs.c',
  'fu-smbios.c', # fuzzing
  'fu-srec-firmware.c', # fuzzing
  'fu-string.c', # fuzzing
  'fu-sum.c', # fuzzing
  'fu-udev-device.c',
  'fu-usb-device.c',
  'fu-usb-device-ds20.c', # fuzzing
  'fu-usb-device-fw-ds20.c', # fuzzing
  'fu-usb-device-ms-ds20.c', # fuzzing
  'fu-uswid-firmware.c', # fuzzing
  'fu-version.c',
  'fu-version-common.c', # fuzzing
  'fu-volume.c', # fuzzing
]

if host_machine.system() == 'linux'
  fwupdplugin_src += 'fu-common-linux.c' # fuzzing
  fwupdplugin_src += 'fu-efivar-linux.c'
elif host_machine.system() == 'freebsd'
  fwupdplugin_src += 'fu-common-freebsd.c'
  fwupdplugin_src += 'fu-efivar-freebsd.c'
elif host_machine.system() == 'windows'
  fwupdplugin_src += 'fu-common-windows.c'
  fwupdplugin_src += 'fu-efivar-windows.c'
elif host_machine.system() == 'darwin'
  fwupdplugin_src += 'fu-common-darwin.c'
  fwupdplugin_src += 'fu-efivar-darwin.c'
else
  fwupdplugin_src += 'fu-efivar-impl.c' # fuzzing
endif

fwupdplugin_headers = [
  'fu-acpi-table.h',
  'fu-archive-firmware.h',
  'fu-archive.h',
  'fu-backend.h',
  'fu-backend-private.h',
  'fu-bios-settings.h',
  'fu-bios-settings-private.h',
  'fu-bluez-device.h',
  'fu-byte-array.h',
  'fu-bytes.h',
  'fu-cabinet.h',
  'fu-cfi-device.h',
  'fu-cfu-common.h',
  'fu-cfu-offer.h',
  'fu-cfu-payload.h',
  'fu-chunk.h',
  'fu-common-guid.h',
  'fu-common.h',
  'fu-config.h',
  'fu-config-private.h',
  'fu-context.h',
  'fu-context-private.h',
  'fu-coswid-firmware.h',
  'fu-crc.h',
  'fu-csv-entry.h',
  'fu-csv-firmware.h',
  'fu-device.h',
  'fu-device-locker.h',
  'fu-device-metadata.h',
  'fu-device-private.h',
  'fu-device-progress.h',
  'fu-dfu-firmware.h',
  'fu-dfuse-firmware.h',
  'fu-dump.h',
  'fu-efi-common.h',
  'fu-efi-firmware-file.h',
  'fu-efi-firmware-filesystem.h',
  'fu-efi-firmware-section.h',
  'fu-efi-firmware-volume.h',
  'fu-efi-load-option.h',
  'fu-efi-signature.h',
  'fu-efi-signature-list.h',
  'fu-efivar.h',
  'fu-elf-firmware.h',
  'fu-fdt-firmware.h',
  'fu-fdt-image.h',
  'fu-firmware-common.h',
  'fu-firmware.h',
  'fu-fit-firmware.h',
  'fu-fmap-firmware.h',
  'fu-hid-device.h',
  'fu-hwids.h',
  'fu-i2c-device.h',
  'fu-ifd-bios.h',
  'fu-ifd-common.h',
  'fu-ifd-firmware.h',
  'fu-ifd-image.h',
  'fu-ifwi-cpd-firmware.h',
  'fu-ifwi-fpt-firmware.h',
  'fu-ihex-firmware.h',
  'fu-intel-thunderbolt-firmware.h',
  'fu-intel-thunderbolt-nvm.h',
  'fu-io-channel.h',
  'fu-kenv.h',
  'fu-kernel.h',
  'fu-linear-firmware.h',
  'fu-mei-device.h',
  'fu-mem.h',
  'fu-mem-private.h',
  'fu-oprom-firmware.h',
  'fu-path.h',
  'fu-pefile-firmware.h',
  'fu-plugin.h',
  'fu-plugin-private.h',
  'fu-progress.h',
  'fu-quirks.h',
  'fu-sbatlevel-section.h',
  'fu-security-attr.h',
  'fu-security-attrs.h',
  'fu-security-attrs-private.h',
  'fu-smbios.h',
  'fu-smbios-private.h',
  'fu-srec-firmware.h',
  'fu-string.h',
  'fu-sum.h',
  'fu-udev-device.h',
  'fu-udev-device-private.h',
  'fu-usb-device-ds20.h',
  'fu-usb-device-fw-ds20.h',
  'fu-usb-device.h',
  'fu-usb-device-ms-ds20.h',
  'fu-usb-device-private.h',
  'fu-uswid-firmware.h',
  'fu-version-common.h',
  'fu-volume.h',
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

fwupdplugin = library(
  'fwupdplugin',
  fwupdplugin_struct_targets,
  sources: [
    fwupdplugin_src,
    fwupdplugin_headers,
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
      fwupdplugin_struct_targets,
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
endif

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'fwupdplugin-self-test',
    installed_firmware_zip,
    rustgen.process('fu-self-test.rs'),
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
      '-DSRCDIR="' + meson.current_source_dir() + '"',
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
