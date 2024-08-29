if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginAlgoltekUsbcardreader"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('algoltek-usbcardreader.quirk')
plugin_builtins += static_library('fu_plugin_algoltek_usbcardreader',
  rustgen.process('fu-algoltek-usbcardreader.rs'),
  sources: [
    'fu-algoltek-usbcardreader-device.c',
    'fu-algoltek-usbcardreader-firmware.c',
    'fu-algoltek-usbcardreader-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
