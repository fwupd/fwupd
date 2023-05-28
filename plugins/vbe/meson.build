cargs = ['-DG_LOG_DOMAIN="FuPluginVbe"']

plugin_builtins += static_library(
  'fu_plugin_vbe',
  sources: [
    'fu-vbe-plugin.c',
    'fu-vbe-device.c',
    'fu-vbe-simple-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies: [
    plugin_deps,
  ],
)
