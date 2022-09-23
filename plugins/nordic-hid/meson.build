if gudev.found() and gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginNordicHid"']

plugin_quirks += files('nordic-hid.quirk')
plugin_builtins += static_library('fu_plugin_nordic_hid',
  sources: [
    'fu-nordic-hid-plugin.c',
    'fu-nordic-hid-cfg-channel.c',
    'fu-nordic-hid-firmware.c',
    'fu-nordic-hid-firmware-b0.c',
    'fu-nordic-hid-firmware-mcuboot.c',
    'fu-nordic-hid-archive.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
