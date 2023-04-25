if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixtp"']

plugin_quirks += files('goodixtp.quirk')
plugin_builtins += static_library('fu_plugin_goodixtp',
  sources: [
    'fu-goodixtp-hid-device.c',
    'fu-goodixtp-plugin.c',
    'fu-goodixtp-common.c',
    'fu-goodixtp-gtx8.c',
    'fu-goodixtp-brlb.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
