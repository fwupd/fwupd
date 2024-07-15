if get_option('plugin_fastboot').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginFastboot"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('fastboot.quirk')
plugin_builtins += static_library('fu_plugin_fastboot',
  sources: [
    'fu-fastboot-plugin.c',
    'fu-fastboot-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
