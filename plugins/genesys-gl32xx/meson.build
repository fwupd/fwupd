if enable_udev
cargs = ['-DG_LOG_DOMAIN="FuPluginGenesysGl32xx"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('genesys-gl32xx.quirk')
plugin_builtins += static_library('fu_plugin_genesys_gl32xx',
  sources: [
    'fu-genesys-gl32xx-device.c',
    'fu-genesys-gl32xx-firmware.c',
    'fu-genesys-gl32xx-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
