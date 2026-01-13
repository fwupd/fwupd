host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginRaydiumtp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('raydium-tp.quirk')
plugin_builtin_raydium_tp = static_library('fu_plugin_raydium_tp',
  rustgen.process(
    'fu-raydium-tp.rs',
  ),
  sources: [
    'fu-raydium-tp-plugin.c',
    'fu-raydium-tp-firmware.c',
    'fu-raydium-tp-hid-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)

plugin_builtins += plugin_builtin_raydium_tp