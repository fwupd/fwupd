if allow_uefi
cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dell.quirk')

plugin_builtin_dell = static_library('fu_plugin_dell',
  sources: [
    'fu-dell-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: [
    cargs,
  ],
  dependencies: [
    plugin_deps,
  ],
)
plugin_builtins += plugin_builtin_dell

endif
