gpio_header = cc.has_header_symbol('linux/gpio.h', 'GPIO_V2_LINE_FLAG_OUTPUT', required: get_option('plugin_gpio'))

if gpio_header and gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginGpio"']

install_data([
  'gpio.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_gpio',
  fu_hash,
  sources : [
    'fu-plugin-gpio.c',
    'fu-gpio-device.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  dependencies : [
    plugin_deps,
  ],
)
endif
