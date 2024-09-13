fwupdplugin_structs = [
  'fu-common.rs', # fuzzing
  'fu-acpi-table.rs', # fuzzing
  'fu-archive.rs', # fuzzing
  'fu-cab.rs', # fuzzing
  'fu-cfu-firmware.rs', # fuzzing
  'fu-coswid.rs', # fuzzing
  'fu-dfu-firmware.rs', # fuzzing
  'fu-dpaux.rs', # fuzzing
  'fu-edid.rs', # fuzzing
  'fu-efi.rs', # fuzzing
  'fu-elf.rs', # fuzzing
  'fu-fdt.rs', # fuzzing
  'fu-fmap.rs', # fuzzing
  'fu-hid.rs', # fuzzing
  'fu-ifd.rs', # fuzzing
  'fu-ifwi.rs', # fuzzing
  'fu-intel-thunderbolt.rs', # fuzzing
  'fu-io-channel.rs', # fuzzing
  'fu-msgpack.rs', # fuzzing
  'fu-oprom.rs', # fuzzing
  'fu-pefile.rs', # fuzzing
  'fu-progress.rs', # fuzzing
  'fu-sbatlevel-section.rs', # fuzzing
  'fu-smbios.rs', # fuzzing
  'fu-usb-device-ds20.rs', # fuzzing
  'fu-usb.rs', # fuzzing
  'fu-uswid.rs', # fuzzing
  'fu-v4l.rs', # fuzzing
]

# do not use structgen as these are referenced in headers too...
fwupdplugin_rs_targets = []
foreach fwupdplugin_struct: fwupdplugin_structs
  fwupdplugin_rs_targets += custom_target('lib' + fwupdplugin_struct,
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
  'fu-backend.c', # fuzzing
  'fu-bios-setting.c', # fuzzing
  'fu-bios-settings.c', # fuzzing
  'fu-block-device.c',
  'fu-bluez-device.c',
  'fu-byte-array.c', # fuzzing
  'fu-bytes.c', # fuzzing
  'fu-cab-firmware.c', # fuzzing
  'fu-cab-image.c', # fuzzing
  'fu-cfi-device.c',
  'fu-cfu-offer.c', # fuzzing
  'fu-cfu-payload.c', # fuzzing
  'fu-chunk.c', # fuzzing
  'fu-chunk-array.c', # fuzzing
  'fu-common.c', # fuzzing
  'fu-common-guid.c',
  'fu-composite-input-stream.c', # fuzzing
  'fu-config.c', # fuzzing
  'fu-context.c', # fuzzing
  'fu-coswid-common.c', # fuzzing
  'fu-coswid-firmware.c', # fuzzing
  'fu-crc.c', # fuzzing
  'fu-csv-entry.c', # fuzzing
  'fu-csv-firmware.c', # fuzzing
  'fu-device.c', # fuzzing
  'fu-device-event.c', # fuzzing
  'fu-device-locker.c', # fuzzing
  'fu-device-progress.c',
  'fu-dfu-firmware.c', # fuzzing
  'fu-dfuse-firmware.c', # fuzzing
  'fu-dpaux-device.c',
  'fu-drm-device.c',
  'fu-dummy-efivars.c', # fuzzing
  'fu-dump.c', # fuzzing
  'fu-edid.c', # fuzzing
  'fu-efi-common.c', # fuzzing
  'fu-efi-device-path.c', # fuzzing
  'fu-efi-device-path-list.c', # fuzzing
  'fu-efi-file-path-device-path.c', # fuzzing
  'fu-efi-file.c', # fuzzing
  'fu-efi-filesystem.c', # fuzzing
  'fu-efi-section.c', # fuzzing
  'fu-efi-volume.c', # fuzzing
  'fu-efi-lz77-decompressor.c', # fuzzing
  'fu-efi-hard-drive-device-path.c', # fuzzing
  'fu-efi-load-option.c', # fuzzing
  'fu-efi-signature.c',
  'fu-efi-signature-list.c',
  'fu-efivars.c', # fuzzing
  'fu-elf-firmware.c', # fuzzing
  'fu-fdt-firmware.c', # fuzzing
  'fu-fdt-image.c', # fuzzing
  'fu-firmware.c', # fuzzing
  'fu-firmware-common.c', # fuzzing
  'fu-fit-firmware.c', # fuzzing
  'fu-fmap-firmware.c', # fuzzing
  'fu-hid-descriptor.c', # fuzzing
  'fu-hid-device.c',
  'fu-hid-report-item.c', # fuzzing
  'fu-hid-report.c', # fuzzing
  'fu-hidraw-device.c',
  'fu-hwids.c', # fuzzing
  'fu-hwids-config.c', # fuzzing
  'fu-hwids-dmi.c', # fuzzing
  'fu-hwids-fdt.c', # fuzzing
  'fu-hwids-kenv.c', # fuzzing
  'fu-hwids-darwin.c', # fuzzing
  'fu-hwids-smbios.c', # fuzzing
  'fu-i2c-device.c',
  'fu-ifd-bios.c', # fuzzing
  'fu-ifd-common.c', # fuzzing
  'fu-ifd-firmware.c', # fuzzing
  'fu-ifd-image.c', # fuzzing
  'fu-ifwi-cpd-firmware.c', # fuzzing
  'fu-ifwi-fpt-firmware.c', # fuzzing
  'fu-ihex-firmware.c', # fuzzing
  'fu-input-stream.c', # fuzzing
  'fu-intel-thunderbolt-firmware.c', # fuzzing
  'fu-intel-thunderbolt-nvm.c', # fuzzing
  'fu-io-channel.c', # fuzzing
  'fu-kenv.c', # fuzzing
  'fu-kernel.c', # fuzzing
  'fu-linear-firmware.c',
  'fu-lzma-common.c', # fuzzing
  'fu-mei-device.c',
  'fu-mem.c', # fuzzing
  'fu-msgpack.c',
  'fu-msgpack-item.c',
  'fu-oprom-firmware.c', # fuzzing
  'fu-partial-input-stream.c', # fuzzing
  'fu-path.c', # fuzzing
  'fu-pefile-firmware.c', # fuzzing
  'fu-pci-device.c',
  'fu-plugin.c',
  'fu-progress.c', # fuzzing
  'fu-quirks.c', # fuzzing
  'fu-sbatlevel-section.c', # fuzzing
  'fu-security-attr.c', # fuzzing
  'fu-security-attrs.c',
  'fu-serio-device.c',
  'fu-smbios.c', # fuzzing
  'fu-srec-firmware.c', # fuzzing
  'fu-string.c', # fuzzing
  'fu-sum.c', # fuzzing
  'fu-udev-device.c',
  'fu-usb-bos-descriptor.c',
  'fu-usb-config-descriptor.c',
  'fu-usb-descriptor.c',
  'fu-usb-device.c',
  'fu-usb-device-ds20.c',
  'fu-usb-device-fw-ds20.c',
  'fu-usb-device-ms-ds20.c',
  'fu-usb-endpoint.c',
  'fu-usb-interface.c',
  'fu-uswid-firmware.c', # fuzzing
  'fu-version-common.c', # fuzzing
  'fu-v4l-device.c',
  'fu-volume.c', # fuzzing
]

if host_machine.system() == 'linux'
  fwupdplugin_src += 'fu-common-linux.c' # fuzzing
  fwupdplugin_src += 'fu-linux-efivars.c'
elif host_machine.system() == 'freebsd'
  fwupdplugin_src += 'fu-common-freebsd.c'
  fwupdplugin_src += 'fu-freebsd-efivars.c'
elif host_machine.system() == 'windows'
  fwupdplugin_src += 'fu-common-windows.c'
  fwupdplugin_src += 'fu-windows-efivars.c'
elif host_machine.system() == 'darwin'
  fwupdplugin_src += 'fu-common-darwin.c'
  fwupdplugin_src += 'fu-darwin-efivars.c' # fuzzing
endif

fwupdplugin_headers = [
  'fu-acpi-table.h',
  'fu-archive-firmware.h',
  'fu-archive.h',
  'fu-backend.h',
  'fu-bios-settings.h',
  'fu-bios-settings-private.h',
  'fu-block-device.h',
  'fu-bluez-device.h',
  'fu-byte-array.h',
  'fu-bytes.h',
  'fu-cab-firmware.h',
  'fu-cab-image.h',
  'fu-cfi-device.h',
  'fu-cfu-offer.h',
  'fu-cfu-payload.h',
  'fu-chunk.h',
  'fu-chunk-array.h',
  'fu-common-guid.h',
  'fu-common.h',
  'fu-composite-input-stream.h',
  'fu-config.h',
  'fu-config-private.h',
  'fu-context.h',
  'fu-context-private.h',
  'fu-coswid-common.h',
  'fu-coswid-firmware.h',
  'fu-crc.h',
  'fu-csv-entry.h',
  'fu-csv-firmware.h',
  'fu-device.h',
  'fu-device-event.h',
  'fu-device-locker.h',
  'fu-device-metadata.h',
  'fu-device-private.h',
  'fu-device-progress.h',
  'fu-dfu-firmware.h',
  'fu-dfuse-firmware.h',
  'fu-dpaux-device.h',
  'fu-drm-device.h',
  'fu-dump.h',
  'fu-edid.h',
  'fu-efi-common.h',
  'fu-efi-device-path.h',
  'fu-efi-device-path-list.h',
  'fu-efi-file.h',
  'fu-efi-filesystem.h',
  'fu-efi-hard-drive-device-path.h',
  'fu-efi-section.h',
  'fu-efi-volume.h',
  'fu-efi-load-option.h',
  'fu-efi-signature.h',
  'fu-efi-signature-list.h',
  'fu-efivars.h',
  'fu-elf-firmware.h',
  'fu-endian.h',
  'fu-fdt-firmware.h',
  'fu-fdt-image.h',
  'fu-firmware-common.h',
  'fu-firmware.h',
  'fu-fit-firmware.h',
  'fu-fmap-firmware.h',
  'fu-hid-descriptor.h',
  'fu-hid-device.h',
  'fu-hid-report.h',
  'fu-hidraw-device.h',
  'fu-hwids.h',
  'fu-i2c-device.h',
  'fu-ifd-bios.h',
  'fu-ifd-common.h',
  'fu-ifd-firmware.h',
  'fu-ifd-image.h',
  'fu-ifwi-cpd-firmware.h',
  'fu-ifwi-fpt-firmware.h',
  'fu-ihex-firmware.h',
  'fu-input-stream.h',
  'fu-intel-thunderbolt-firmware.h',
  'fu-intel-thunderbolt-nvm.h',
  'fu-io-channel.h',
  'fu-kenv.h',
  'fu-kernel.h',
  'fu-linear-firmware.h',
  'fu-mei-device.h',
  'fu-mem.h',
  'fu-mem-private.h',
  'fu-msgpack-item.h',
  'fu-oprom-firmware.h',
  'fu-partial-input-stream.h',
  'fu-partial-input-stream-private.h',
  'fu-path.h',
  'fu-pefile-firmware.h',
  'fu-pci-device.h',
  'fu-plugin.h',
  'fu-plugin-private.h',
  'fu-progress.h',
  'fu-quirks.h',
  'fu-sbatlevel-section.h',
  'fu-security-attr.h',
  'fu-security-attrs.h',
  'fu-security-attrs-private.h',
  'fu-serio-device.h',
  'fu-smbios.h',
  'fu-smbios-private.h',
  'fu-srec-firmware.h',
  'fu-string.h',
  'fu-sum.h',
  'fu-udev-device.h',
  'fu-udev-device-private.h',
  'fu-usb-device-ds20.h',
  'fu-usb-device-fw-ds20.h',
  'fu-usb-bos-descriptor.h',
  'fu-usb-bos-descriptor-private.h',
  'fu-usb-config-descriptor.h',
  'fu-usb-config-descriptor-private.h',
  'fu-usb-device.h',
  'fu-usb-endpoint.h',
  'fu-usb-endpoint-private.h',
  'fu-usb-interface.h',
  'fu-usb-interface-private.h',
  'fu-usb-device-ms-ds20.h',
  'fu-usb-device-private.h',
  'fu-uswid-firmware.h',
  'fu-v4l-device.h',
  'fu-version-common.h',
  'fu-volume.h',
]

introspection_deps = [
  libxmlb,
  giounix,
  libusb,
]

pkgg_requires = [ 'gio-2.0',
                  'gmodule-2.0',
                  'gobject-2.0',
                  'fwupd',
                  'json-glib-1.0',
                  'libarchive',
                  'xmlb',
                  'libusb',
]

library_deps = [
  introspection_deps,
  gmodule,
  libjsonglib,
  zlib,
  valgrind,
  lzma,
  libarchive,
  libusb,
  cbor,
  sqlite,
  libdrm,
  platform_deps,
]

fwupdplugin = library(
  'fwupdplugin',
  fwupdplugin_rs_targets,
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

# see https://mesonbuild.com/FAQ.html#how-do-i-tell-meson-that-my-sources-use-generated-headers
fwupdplugin_rs_headers = []
foreach fwupdplugin_rs_target: fwupdplugin_rs_targets
  fwupdplugin_rs_headers += fwupdplugin_rs_target[1]
endforeach
fwupdplugin_rs_dep = declare_dependency(link_with: fwupdplugin, sources: fwupdplugin_rs_headers)

if introspection.allowed()
  gir_dep = declare_dependency(sources: fwupd_gir)
  girtargets = []
  extra_args = []
  if libxmlb.type_name() == 'internal'
    girtargets += subproject('libxmlb').get_variable('gir')[0]
  else
    girtargets += 'Xmlb-2.0'
  endif
  fwupdplugin_gir = gnome.generate_gir(fwupd,
    sources: [
      fwupdplugin_src,
      fwupdplugin_headers,
      fwupdplugin_rs_targets,
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
  gcab = executable(
    'gcab',
    sources: [
      'fu-gcab.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
    ],
    dependencies: [
      library_deps,
      fwupdplugin_rs_dep,
    ],
    link_with: [
      fwupd,
      fwupdplugin
    ],
  )

  subdir('tests')

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  e = executable(
    'fwupdplugin-self-test',
    installed_firmware_zip,
    rustgen.process('fu-self-test.rs'),
    sources: [
      'fu-test-device.c',
      'fu-self-test.c'
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
    ],
    dependencies: [
      library_deps,
      fwupdplugin_rs_dep,
    ],
    link_with: [
      fwupd,
      fwupdplugin
    ],
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('fwupdplugin-self-test', e, is_parallel: false, timeout: 180, env: env)
endif

fwupdplugin_incdir = include_directories('.')
