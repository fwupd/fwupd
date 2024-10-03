if get_option('plugin_mediatek_scaler').require(enable_udev,
    error_message: 'udev is needed for plugin_mediatek_scaler').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginMediatekScaler"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('mediatek-scaler.quirk')
plugin_builtins += static_library('fu_plugin_mediatek_scaler',
  rustgen.process(
    'fu-mediatek-scaler.rs', # fuzzing
  ),
  sources: [
    'fu-mediatek-scaler-device.c',
    'fu-mediatek-scaler-firmware.c',
    'fu-mediatek-scaler-common.c',
    'fu-mediatek-scaler-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies: plugin_deps,
)
endif
