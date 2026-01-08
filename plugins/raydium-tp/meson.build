host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginRaydiumtp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('raydiumtp.quirk')
plugin_builtin_raydiumtp = static_library('fu_plugin_raydiumtp',
  rustgen.process(
    'fu-raydiumtp.rs',
  ),
  sources: [
    'fu-raydiumtp-plugin.c',
    'fu-raydiumtp-firmware.c',
    'fu-raydiumtp-hid-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)

plugin_builtins += plugin_builtin_raydiumtp