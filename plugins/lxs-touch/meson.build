host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLxstouch"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('lxs-touch.quirk')
plugin_builtins += static_library('fu_plugin_lxs-touch',
  rustgen.process('fu-lxs-touch.rs'),
  sources: [
    'fu-lxs-touch-plugin.c',
    'fu-lxs-touch-device.c',
    'fu-lxs-touch-firmware.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
