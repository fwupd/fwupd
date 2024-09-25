cargs = ['-DG_LOG_DOMAIN="FuPluginJabraFile"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('jabra-file.quirk')
plugin_builtins += static_library('fu_plugin_jabra_file',
  rustgen.process('fu-jabra-file.rs'),
  sources: [
    'fu-jabra-file-device.c',
    'fu-jabra-file-firmware.c',
    'fu-jabra-file-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
