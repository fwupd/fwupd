cargs = ['-DG_LOG_DOMAIN="FuPluginQcS5gen2"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('qc-s5gen2.quirk')
plugin_builtins += static_library('fu_plugin_qc_s5gen2',
  rustgen.process('fu-qc-s5gen2.rs'),
  rustgen.process('fu-qc-s5gen2-ble.rs'),
  rustgen.process('fu-qc-s5gen2-hid.rs'),
  rustgen.process('fu-qc-s5gen2-fw.rs'),
  sources: [
    'fu-qc-s5gen2-device.c',
    'fu-qc-s5gen2-ble-device.c',
    'fu-qc-s5gen2-hid-device.c',
    'fu-qc-s5gen2-firmware.c',
    'fu-qc-s5gen2-plugin.c',
    'fu-qc-s5gen2-impl.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
