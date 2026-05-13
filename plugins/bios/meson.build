allow_uefi or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginBios"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_bios',
  sources: [
    'fu-bios-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
