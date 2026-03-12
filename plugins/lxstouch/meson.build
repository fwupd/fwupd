cargs = ['-DG_LOG_DOMAIN="FuPluginLxsTouch"']

plugin_quirks += files('fu-lxs-touch.quirk')

shared_module('fu_plugin_lxs_touch',
  sources: [
    'fu-lxs-touch-plugin.c',
    'fu-lxs-touch-device.c',
  ],
  include_directories: plugin_incdirs,
  install: true,
  install_rpath: libdir_pkg,
  install_dir: libdir_pkg,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)
