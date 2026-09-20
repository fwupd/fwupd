host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekEdpscaler"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('realtek-edpscaler.quirk')
plugin_builtins += static_library('fu_plugin_realtek_edpscaler',
  rustgen.process('fu-realtek-edpscaler.rs'),
  sources: [
    'fu-realtek-edpscaler-device.c',
    'fu-realtek-edpscaler-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
