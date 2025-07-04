cargs = ['-DG_LOG_DOMAIN="FuPluginUefiEsrt"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_uefi_esrt',
  sources: [
    'fu-uefi-esrt-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: [
    plugin_deps,
  ],
)

