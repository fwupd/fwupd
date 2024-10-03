if get_option('plugin_emmc').require(enable_udev,
    error_message: 'udev is needed for plugin_emmc').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginEmmc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('emmc.quirk')
plugin_builtins += static_library('fu_plugin_emmc',
  sources: [
    'fu-emmc-plugin.c',
    'fu-emmc-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
