host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLxstouch"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('lxstouch.quirk')
plugin_builtins += static_library('fu_plugin_lxstouch',
  rustgen.process('fu-lxstouch.rs'),
  sources: [
    'fu-lxstouch-plugin.c',
    'fu-lxstouch-device.c',
    'fu-lxstouch-firmware.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
