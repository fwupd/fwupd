host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginThelioIo"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('thelio-io.quirk')
plugin_builtins += static_library('fu_plugin_thelio_io',
  sources: [
    'fu-thelio-io-plugin.c',
    'fu-thelio-io-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
