if get_option('plugin_intel_spi')
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelSpi"']

install_data(['intel-spi.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_intel_spi',
  fu_hash,
  sources : [
    'fu-efi-common.c',              # fuzzing
    'fu-efi-firmware-common.c',     # fuzzing
    'fu-efi-firmware-file.c',       # fuzzing
    'fu-efi-firmware-filesystem.c', # fuzzing
    'fu-efi-firmware-section.c',    # fuzzing
    'fu-efi-firmware-volume.c',     # fuzzing
    'fu-ifd-bios.c',                # fuzzing
    'fu-ifd-common.c',              # fuzzing
    'fu-ifd-device.c',
    'fu-ifd-firmware.c',            # fuzzing
    'fu-ifd-image.c',               # fuzzing
    'fu-intel-spi-common.c',
    'fu-intel-spi-device.c',
    'fu-pci-device.c',
    'fu-plugin-intel-spi.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    lzma,
  ],
)
endif

if get_option('tests') and get_option('lzma')
  e = executable(
    'intel-spi-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-efi-common.c',
      'fu-efi-firmware-common.c',
      'fu-efi-firmware-file.c',
      'fu-efi-firmware-filesystem.c',
      'fu-efi-firmware-section.c',
      'fu-efi-firmware-volume.c',
      'fu-ifd-bios.c',
      'fu-ifd-common.c',
      'fu-ifd-firmware.c',
      'fu-ifd-image.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      lzma,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    install : true,
    install_dir : installed_test_bindir,
  )
  test('intel-spi-self-test', e)
endif
