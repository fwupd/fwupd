host_machine.system() in ['linux', 'android'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginNvtTs"']

plugins += { meson.current_source_dir().split('/')[-1] : true }

plugin_builtins += static_library(
  'fu_plugin_nvt_ts',
  sources: ['fu-nvt-ts-plugin.c'],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
