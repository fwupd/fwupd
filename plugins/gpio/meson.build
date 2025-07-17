host_machine.system() == 'linux' or subdir_done()
cc.has_header_symbol('linux/gpio.h', 'GPIO_V2_LINE_FLAG_OUTPUT', required: false) or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginGpio"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('gpio.quirk')
plugin_builtins += static_library('fu_plugin_gpio',
  sources: [
    'fu-gpio-plugin.c',
    'fu-gpio-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
