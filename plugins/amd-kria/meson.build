if host_machine.system() != 'linux'
  subdir_done()
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginAmdKria"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('kria.quirk')
plugin_builtin_kria = static_library('fu_plugin_amd_kria',
  rustgen.process(
    'fu-amd-kria-persistent.rs',          # fuzzing
    'fu-amd-kria-som-eeprom.rs',          # fuzzing
  ),
  sources: [
    'fu-amd-kria-plugin.c',
    'fu-amd-kria-device.c',
    'fu-amd-kria-image-firmware.c',
    'fu-amd-kria-persistent-firmware.c',
    'fu-amd-kria-som-eeprom.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_kria
