cargs = ['-DG_LOG_DOMAIN="FuPluginAlgoltekUsbcr"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('algoltek-usbcr.quirk')
plugin_builtins += static_library('fu_plugin_algoltek_usbcr',
  rustgen.process('fu-algoltek-usbcr.rs'),
  sources: [
    'fu-algoltek-usbcr-device.c',
    'fu-algoltek-usbcr-firmware.c',
    'fu-algoltek-usbcr-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
