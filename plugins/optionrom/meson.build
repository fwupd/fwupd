if enable_udev
cargs = ['-DG_LOG_DOMAIN="FuPluginOptionrom"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('optionrom.quirk')

plugin_builtins += static_library('fu_plugin_optionrom',
  sources: [
    'fu-optionrom-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
