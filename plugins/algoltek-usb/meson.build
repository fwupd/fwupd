cargs = ['-DG_LOG_DOMAIN="FuPluginAlgoltekUsb"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('algoltek-usb.quirk')
plugin_builtins += static_library('fu_plugin_algoltek_usb',
    rustgen.process('fu-algoltek-usb.rs'),
    sources: [
        'fu-algoltek-usb-device.c',
        'fu-algoltek-usb-firmware.c',
        'fu-algoltek-usb-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
)

device_tests += files(
  'tests/algoltek-ag9421.json',
)
